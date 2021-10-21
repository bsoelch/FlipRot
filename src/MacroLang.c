/*
 ============================================================================
 Name        : MacroLang.c
 Author      : bsoelch
 Description :
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>
#include <locale.h>

#include "Structs.h"
#include "HashMap.h"


static const int MAX_DEPTH=128;

static const int MAP_CAP=2048;

static const int BUFFER_SIZE=2048;
static const int INIT_CAP_PROG=16;

//should be large enough
#define MEM_SIZE 0x1000000

static const size_t CHAR_IO_ADDR=SIZE_MAX;
//LONG_IO only for debug purposes, may be removed later
static const size_t LONG_IO_ADDR=SIZE_MAX-1;

typedef enum{
	READ_ACTION,//read actions
	READ_COMMENT,//read comment
	READ_LABEL,//read name of label
	READ_INCLUDE,//read until start of include-path
	READ_INCLUDE_PATH,//read name of include-path
	READ_MACRO_NAME,//read name of macro
	READ_MACRO_ACTION,//read actions in macro
	READ_UNDEF,//read name for undef
} ReadState;

bool ensureProgCap(Macro* prog){
	if(prog->len>=prog->cap){
		Action* tmp=realloc(prog->actions,2*(prog->cap)*sizeof(Action));
		if(!tmp){
			return false;
		}
		prog->actions=tmp;
		(prog->cap)*=2;
	}
	return true;
}
void freeMacro(Macro m){
	if(m.cap>0){
		for(size_t i=0;i<m.len;i++){
			switch(m.actions[i].type){
			case INVALID:
			case LOAD_INT:
			case SWAP:
			case LOAD:
			case STORE:
			case JUMPIF:
			case ROT:
			case FLIP:
			case COMMENT_START:
			case MACRO_START:
			case END:
				break;//no pointer in data
			case INCLUDE:
			case UNDEF:
			case LABEL:
			case LABEL_DEF:
				free(m.actions[i].data.asString.chars);
			}
		}
		free(m.actions);
	}
}

//test if cStr (NULL-terminated) is equals to str (length len)
//ignores the case of all used chars
int strCaseEq(const char* cStr,char* str,size_t len){
	size_t i=0;
	for(;i<len;i++){
		if(toupper(cStr[i])!=toupper(str[i])){
			return 0;
		}
	}
	return cStr[i]=='\0';
}
Action loadAction(char* buf,size_t lenBuf){
	Action ret={.type=INVALID,.data.asInt=0};
	char* tail=buf+lenBuf;
	long long ll=strtoull(buf,&tail,0);
	if(tail==buf+lenBuf){
		ret.type=LOAD_INT;
		ret.data.asInt=ll;
	}else if(strCaseEq("SWAP",buf,lenBuf)){
		ret.type=SWAP;
	}else if(strCaseEq("LOAD",buf,lenBuf)){
		ret.type=LOAD;
	}else if(strCaseEq("STORE",buf,lenBuf)){
		ret.type=STORE;
	}else if(strCaseEq("JUMPIF",buf,lenBuf)){
		ret.type=JUMPIF;
	}else if(strCaseEq("ROT",buf,lenBuf)){
		ret.type=ROT;
	}else if(strCaseEq("FLIP",buf,lenBuf)){
		ret.type=FLIP;
	}else if(strCaseEq("#__",buf,lenBuf)){//comment
		ret.type=COMMENT_START;
	}else if(strCaseEq("#include",buf,lenBuf)){
		ret.type=INCLUDE;
		ret.data.asString=(String){.len=0,.chars=NULL};
	}else if(strCaseEq("#undef",buf,lenBuf)){
		ret.type=UNDEF;
		ret.data.asString=(String){.len=0,.chars=NULL};
	}else if(strCaseEq("#label",buf,lenBuf)){
		ret.type=LABEL_DEF;
		ret.data.asString=(String){.len=0,.chars=NULL};
	}else if(strCaseEq("#def",buf,lenBuf)){
		ret.type=MACRO_START;
	}else if(strCaseEq("#end",buf,lenBuf)){
		ret.type=END;
	}
	return ret;
}

static String getDirFromPath(char* path,size_t pathLen){
	String dir={.len=pathLen,.chars=path};
	bool isFile=true;
	do{
		switch(dir.chars[--dir.len]){
		case '/'://XXX better handling of path separators
		case '\\':
		case ':':
			dir.len++;
			isFile=false;
			break;
		}
	}while(isFile&&dir.len>0);
	return dir;
}

int readFile(FILE* file,Program* prog,HashMap* macroMap,String localDir,int depth);

int include(Program* prog,HashMap* macroMap,String path,String localDir,int depth){
	size_t pathLen=path.len+localDir.len;
	char* filePath=malloc(pathLen+1);
	int r=0;
	if(!filePath){
		return ERR_MEM;//out of memory
	}
	filePath[pathLen]='\0';
	memcpy(filePath,localDir.chars,localDir.len);
	memcpy(filePath+localDir.len,path.chars,path.len);
	FILE* file=fopen(filePath,"r");
	if(!file&&localDir.len>0){//try as absolute path
		memcpy(filePath,path.chars,path.len);
		pathLen=path.len;
		filePath[path.len]='\0';
	}
	if(file){
		r=readFile(file,prog,macroMap,getDirFromPath(filePath,pathLen),depth+1);
		fclose(file);
	}else{
		r=ERR_FILE_NOT_FOUND;
	}
	free(filePath);
	return r;
}

//returns true if and error occurs
bool putLabel(Program* prog,HashMap* map,String label){
	Mapable prev=mapPut(map,label,
			(Mapable){.type=MAPABLE_POS,.value.asPos=prog->len});
	switch(prev.type){
	case MAPABLE_NONE:
		if(prev.value.asPos!=0){
			return true;
		}
		break;
	case MAPABLE_POSARRAY:
		for(size_t i=0;i<prev.value.asPosArray.len;i++){
			prog->actions[prev.value.asPosArray.data[i]]=
			(Action){
					.type=LOAD_INT,
					.data.asInt=prog->len
			};
			//XXX check previous data
		}
		freeMapable(prev);
		break;
	case MAPABLE_POS:
	case MAPABLE_MACRO:
		fprintf(stderr,"redefinition of label %.*s\n",(int)label.len,label.chars);
		freeMapable(prev);
		return true;
		break;
	}
	return false;
}

//returns true if and error occurs
bool defineMacro(HashMap* macroMap,String name,Macro* macro){
	Mapable prev=mapPut(macroMap,name,(Mapable){.type=MAPABLE_MACRO,
		.value.asMacro=*macro});
	if(prev.type!=MAPABLE_NONE){
		fprintf(stderr,"redefinition of macro %.*s\n",(int)name.len,name.chars);
		freeMapable(prev);
		return true;
	}else if(prev.value.asPos!=0){
		return true;
	}
	macro->actions=NULL;
	macro->cap=0;
	macro->len=0;
	return false;
}

String copyString(String source){
	if(source.len==0){
		return (String){.len=0,.chars=NULL};
	}else{
		String str;
		str.chars=malloc(source.len);
		if(!str.chars){
			return (String){.len=0,.chars=NULL};
		}
		memcpy(str.chars,source.chars,source.len);
		str.len=source.len;
		return str;
	}
}
Action addUnresolvedLabel(Mapable get,Program* prog,HashMap* map,String label){
	String str=copyString(label);
	if(str.len!=label.len){
		return (Action){.type=INVALID,.data.asInt=ERR_MEM};
	}
	get.value.asPosArray.data[get.value.asPosArray.len++]=prog->len;
	Mapable res=mapPut(map,str,get);
	if(res.type==MAPABLE_NONE&&res.value.asPos!=0){
		return (Action){.type=INVALID,.data.asInt=res.value.asPos};
	}
	//don't use label to prevent double free
	return (Action){.type=LABEL,.data.asString={.len=0,.chars=NULL}};
}
Action resolveLabel(Program* prog,HashMap* macroMap,String label,String localDir,int depth){
	if(depth>MAX_DEPTH){
		return (Action){.type=INVALID,.data.asInt=ERR_EXPANSION_OVERFLOW};
	}
	Mapable get=mapGet(macroMap,label);
	switch(get.type){
	case MAPABLE_NONE://no break
		get.type=MAPABLE_POSARRAY;
		get.value.asPosArray.cap=INIT_CAP_PROG;
		get.value.asPosArray.len=0;
		get.value.asPosArray.data=malloc(INIT_CAP_PROG*sizeof(size_t));
		if(!get.value.asPosArray.data){
			return (Action){.type=INVALID,.data.asInt=ERR_MEM};
		}
		return addUnresolvedLabel(get,prog,macroMap,label);
	case MAPABLE_POSARRAY:
		if(get.value.asPosArray.len>=get.value.asPosArray.cap){
			size_t* tmp=realloc(get.value.asPosArray.data,
					2*get.value.asPosArray.cap*sizeof(size_t));
			if(!tmp){
				return (Action){.type=INVALID,.data.asInt=ERR_MEM};
			}
			get.value.asPosArray.data=tmp;
			get.value.asPosArray.cap*=2;
		}
		return addUnresolvedLabel(get,prog,macroMap,label);
	case MAPABLE_POS:
		return (Action){.type=LOAD_INT,.data.asInt=get.value.asPos};
		break;
	case MAPABLE_MACRO:;
		String str;
		for(size_t i=0;i<get.value.asMacro.len;i++){
			switch(get.value.asMacro.actions[i].type){
			case INVALID://invalid action
			case COMMENT_START:
			case MACRO_START:
			case END:
				return (Action){.type=INVALID,.data.asInt=ERR_UNRESOLVED_MACRO};
			case UNDEF:;
				Mapable prev=mapPut(macroMap,get.value.asMacro.actions[i].data.asString,
						(Mapable){.type=MAPABLE_NONE,.value.asPos=0});
				if(prev.type==MAPABLE_NONE&&prev.value.asPos!=0){
					return (Action){.type=INVALID,.data.asInt=prev.value.asPos};
				}
				break;
			case LABEL_DEF:
				str=copyString(get.value.asMacro.actions[i].data.asString);
				if(str.len!=get.value.asMacro.actions[i].data.asString.len){
					return (Action){.type=INVALID,.data.asInt=ERR_MEM};
				}
				int err=putLabel(prog,macroMap,str);
				if(err){
					return (Action){.type=INVALID,.data.asInt=err};
				}
				break;
			case INCLUDE:;
				int r=include(prog,macroMap,get.value.asMacro.actions[i].data.asString,
						localDir,depth+1);
				if(!r){
					return (Action){.type=INVALID,.data.asInt=r};
				}
				break;
			case LABEL:
				str=copyString(get.value.asMacro.actions[i].data.asString);
				if(str.len!=get.value.asMacro.actions[i].data.asString.len){
					return (Action){.type=INVALID,.data.asInt=ERR_MEM};
				}
				Action a=resolveLabel(prog,macroMap,str,localDir,depth+1);
				if(a.type==INVALID){
					if(a.data.asInt!=0){
						return a;
					}else{
						break;
					}
				}
				if(!ensureProgCap(prog)){
					return (Action){.type=INVALID,.data.asInt=ERR_MEM};
				}
				prog->actions[prog->len++]=a;
				break;
			case LOAD_INT:
			case SWAP:
			case LOAD:
			case STORE:
			case JUMPIF:
			case ROT:
			case FLIP:
				if(!ensureProgCap(prog)){
					return (Action){.type=INVALID,.data.asInt=ERR_MEM};
				}
				prog->actions[prog->len++]=get.value.asMacro.actions[i];
				break;
			}
		}
		break;
	}
	return (Action){.type=INVALID,.data.asInt=NO_ERR};
}

//XXX allow definition of macros in macros
int readFile(FILE* file,Program* prog,HashMap* macroMap,String localDir,int depth){
	if(depth>MAX_DEPTH){
		return 11;//exceeded maximum include depth
	}
	int res=NO_ERR;//TODO report positions of syntax errors
	size_t off,i,rem,prev=0,sCap=0;
	char* buffer=malloc(BUFFER_SIZE);
	char* s=NULL;
	if(!prog->actions){
		fputs("resetting prog",stderr);
		prog->actions=malloc(INIT_CAP_PROG*sizeof(Action));
		prog->cap=INIT_CAP_PROG;
		prog->len=0;
	}
	if(!(buffer&&prog->actions&&macroMap)){
		res=ERR_MEM;
		goto errorCleanup;
	}
	ReadState state=READ_ACTION,prevState;
	Macro tmpMacro;//XXX? stack
	tmpMacro.actions=NULL;
	tmpMacro.cap=0;
	String name,macroName;
	name.chars=NULL;
	if(!prog->actions){
		res=ERR_MEM;
		goto errorCleanup;
	}
	while(1){
		off=i=0;
		rem=fread(buffer,1,BUFFER_SIZE,file);
		if(rem==0){
			if(ferror(stdin)){
				res=ERR_IO;
				goto errorCleanup;
			}else{
				break;
			}
		}
		while(rem>0){
			if(isspace(buffer[i])){
				if(i>off){
					char* t;
					size_t t_len;
					if(prev>0){
						if(prev+i-off>sCap){
							char* t=realloc(s,prev+i-off);
							if(!t){
								res=ERR_MEM;
								goto errorCleanup;
							}
							sCap=prev+i-off;
							s=t;
						}
						memcpy(s+prev,buffer+off,i-off);
						t=s;
						t_len=prev+i-off;
						prev=0;
					}else{
						t=buffer+off;
						t_len=i-off;
					}
					Action a=loadAction(t,t_len);
					//XXX handle comments independent of actions
					switch(state){
					case READ_COMMENT:
						if(a.type==END){
							state=prevState;
						}
						break;
					case READ_INCLUDE:
						if(t_len>0){
							if(t[0]=='"'){
								if(t[t_len-1]=='"'){
									res=include(prog,macroMap,(String){.chars=t+1,.len=t_len-2}
									,localDir,depth);
									if(res){
										goto errorCleanup;
									}
									state=prevState;
								}else{//FIXME handling of whitespaces in filenames
									//whitespace handling has to be performed  outside the isspace block
									name.chars=malloc(t_len-1);
									if(!name.chars){
										res=ERR_MEM;
										goto errorCleanup;
									}
									memcpy(name.chars,t+1,t_len-1);
									name.len=t_len-1;
									state=READ_INCLUDE_PATH;
								}
							}else{
								res=include(prog,macroMap,(String){.chars=t,.len=t_len}
								,localDir,depth);
								state=prevState;
								if(res){
									goto errorCleanup;
								}
							}
						}
						break;
					case READ_INCLUDE_PATH:;
						char* tmp=realloc(name.chars,name.len+t_len);
						if(!tmp){
							res=ERR_MEM;
							goto errorCleanup;
						}
						name.chars=tmp;
						memcpy(name.chars+name.len,t,t_len);
						name.len+=t_len;
						if(name.chars[name.len-1]=='"'){
							name.len--;
							res=include(prog,macroMap,name,localDir,depth);
							state=prevState;
							if(res){
								goto errorCleanup;
							}
							free(name.chars);
							name.chars=NULL;
							name.len=0;
						}
						break;
					case READ_LABEL:
					case READ_UNDEF:
					case READ_MACRO_NAME:
						if((t_len<1)||(loadAction(t,t_len).type!=INVALID)){
							res=ERR_INVALID_IDENTIFER;
							goto errorCleanup;
						}
						name.chars=malloc(t_len);
						if(!name.chars){
							res=ERR_MEM;
							goto errorCleanup;
						}
						memcpy(name.chars,t,t_len);
						name.len=t_len;
						switch(state){
						case READ_UNDEF:
							if(prevState==READ_ACTION){
								Mapable prev=mapPut(macroMap,name,
										(Mapable){.type=MAPABLE_NONE,.value.asPos=0});
								if(prev.type==MAPABLE_NONE&&prev.value.asPos!=0){
									res=prev.value.asPos;
									goto errorCleanup;
								}
							}else{
								if(!ensureProgCap(&tmpMacro)){
									res=ERR_MEM;
									goto errorCleanup;
								}
								tmpMacro.actions[tmpMacro.len++]=(Action){
									.type=UNDEF,.data.asString=name};
							}
							name.chars=NULL;//unlink to prevent double free
							name.len=0;
							state=prevState;
							break;
						case READ_LABEL:
							if(prevState==READ_ACTION){
								res=putLabel(prog,macroMap,name);
								if(res){
									goto errorCleanup;
								}
							}else{
								if(!ensureProgCap(&tmpMacro)){
									res=ERR_MEM;
									goto errorCleanup;
								}
								tmpMacro.actions[tmpMacro.len++]=(Action){
									.type=LABEL_DEF,.data.asString=name};
							}
							name.chars=NULL;//unlink to prevent double free
							name.len=0;
							state=prevState;
							break;
						case READ_MACRO_NAME:
							macroName=name;
							name.chars=NULL;
							name.len=0;
							state=READ_MACRO_ACTION;//name is handled at end of macro
							break;
						default:
							assert(0&&"unreachable");
						}
						break;
					case READ_ACTION:
					case READ_MACRO_ACTION:
						switch(a.type){
						case COMMENT_START:
							prevState=state;
							state=READ_COMMENT;
							break;
						case LABEL://loadAction does not return type LABEL
							assert(false&&"unreachable");
							break;
						case INCLUDE:
							prevState=state;
							state=READ_INCLUDE;
							break;
						case UNDEF:
							prevState=state;
							state=READ_UNDEF;
							break;
						case LABEL_DEF:
							prevState=state;
							state=READ_LABEL;
							break;
						case MACRO_START:
							if(state==READ_ACTION){
								state=READ_MACRO_NAME;
								tmpMacro=(Macro){
									.cap=INIT_CAP_PROG,
									.len=0,
									.actions=malloc(INIT_CAP_PROG*sizeof(Action))
								};
								if(!tmpMacro.actions){
									res=ERR_MEM;
									goto errorCleanup;
								}
							}else{
								res=ERR_UNEXPECTED_MACRO_DEF;
								goto errorCleanup;
							}
							break;
						case END:
							if(state==READ_MACRO_ACTION){
								res=defineMacro(macroMap,macroName,&tmpMacro);
								if(res){
									tmpMacro.actions=NULL;//undef to prevent double free
									tmpMacro.cap=0;
									tmpMacro.len=0;
									goto errorCleanup;
								}
								name.chars=NULL;//undef to prevent double free
								name.len=0;
								state=READ_ACTION;
							}else{
								res=ERR_UNEXPECTED_END;
								goto errorCleanup;//unexpected end of macro
							}
							break;
						case INVALID:
							if(state==READ_ACTION){
								a=resolveLabel(prog,macroMap,
										(String){.chars=t,.len=t_len},localDir,depth);
								if(a.type==INVALID&&a.data.asInt!=0){
									res=a.data.asInt;
									goto errorCleanup;
								}
							}else{
								a.type=LABEL;
								a.data.asString.chars=malloc(t_len);
								if(!a.data.asString.chars){
									res=ERR_MEM;
									goto errorCleanup;
								}
								memcpy(a.data.asString.chars,t,t_len);
								a.data.asString.len=t_len;
								//fall through to add action
							}
							if(a.type==INVALID){
								break;//do not save label
							}
							//no break
						case LOAD_INT:
						case SWAP:
						case LOAD:
						case STORE:
						case JUMPIF:
						case ROT:
						case FLIP:
							if(state==READ_MACRO_ACTION){
								if(!ensureProgCap(&tmpMacro)){
									res=ERR_MEM;
									goto errorCleanup;
								}
								tmpMacro.actions[tmpMacro.len++]=a;
							}else{
								if(!ensureProgCap(prog)){
									res=ERR_MEM;
									goto errorCleanup;
								}
								prog->actions[prog->len++]=a;
							}
						}
					}

				}
				off=i+1;
			}
			i++;
			rem--;
		}
		if(i>off){
			if (prev+i-off>sCap) {
				char *t = realloc(s, prev + i - off);
				if (!t) {
					res=ERR_MEM;
					goto errorCleanup;
				}
				sCap=prev+i-off;
				s = t;
			}
			memcpy(s + prev, buffer + off, i - off);
			prev+=i-off;
		}
	}
	if(prev>0){
		Action a=loadAction(s,prev);
		switch(state){
		case READ_ACTION:
			switch(a.type){
			case LABEL:
				assert(false&&"unreachable");//loadAction does not return LABEL
				break;
			case INCLUDE:
			case COMMENT_START://unfinished label/undef/comment
			case UNDEF:
			case LABEL_DEF:
				res=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			case MACRO_START://unfinished macro
				res=ERR_UNFINISHED_MACRO;
				goto errorCleanup;
			case END://unexpected end
				res=ERR_UNEXPECTED_END;
				goto errorCleanup;
			case INVALID:
				a=resolveLabel(prog,macroMap,(String){.chars=s,.len=prev},
						localDir,depth);
				if(a.type==INVALID){
					if(a.data.asInt!=0){
						res=a.data.asInt;
						goto errorCleanup;
					}else{
						break;
					}
				}
			//no break
			case LOAD_INT:
			case SWAP:
			case LOAD:
			case STORE:
			case JUMPIF:
			case ROT:
			case FLIP:
				if(!ensureProgCap(prog)){
					res=ERR_MEM;
					goto errorCleanup;
				}
				prog->actions[prog->len++]=a;
				break;
			}
			break;
		case READ_COMMENT:
			if(a.type!=END){
				res=ERR_UNFINISHED_COMMENT;
				goto errorCleanup;
			}
			break;
		case READ_MACRO_ACTION:
			if(a.type!=END){
				res=ERR_UNFINISHED_MACRO;
				goto errorCleanup;
			}
			res=defineMacro(macroMap,macroName,&tmpMacro);
			if(res){
				tmpMacro.actions=NULL;//undef to prevent double free
				tmpMacro.cap=0;
				tmpMacro.len=0;
				goto  errorCleanup;
			}
			name.chars=NULL;//unlink to prevent double free
			name.len=0;
			break;
		case READ_UNDEF:
			if(prevState==READ_MACRO_ACTION){
				res=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			Mapable prevVal=mapPut(macroMap,name,
					(Mapable){.type=MAPABLE_NONE,.value.asPos=0});
			if(prevVal.type==MAPABLE_NONE&&prevVal.value.asPos!=0){
				res=prevVal.value.asPos;
				goto errorCleanup;
			}
			name.chars=NULL;//unlink to prevent double free
			name.len=0;
			break;
		case READ_INCLUDE:
			if(prevState==READ_MACRO_ACTION){
				res=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			if(prev==0){
				res=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			}
			if(s[0]=='"'){
				if(s[prev-1]=='"'){
					res=include(prog,macroMap,(String){.chars=s+1,.len=prev-2}
					,localDir,depth);
					if(res){
						goto errorCleanup;
					}
				}else{
					res=ERR_UNFINISHED_IDENTIFER;
					goto errorCleanup;
				}
			}else{
				res=include(prog,macroMap,(String){.chars=s,.len=prev}
				,localDir,depth);
				if(res){
					goto errorCleanup;
				}
			}
		break;
		case READ_INCLUDE_PATH:
			if(prevState==READ_MACRO_ACTION){
				res=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			char* tmp=realloc(name.chars,name.len+prev);
			if(!tmp){
				res=ERR_MEM;
				goto errorCleanup;
			}
			name.chars=tmp;
			memcpy(name.chars+name.len,s,prev);
			name.len+=prev;
			if(name.chars[name.len-1]=='"'){
				name.len--;
				res=include(prog,macroMap,name,localDir,depth);
				if(res){
					goto errorCleanup;
				}
			}else{
				res=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			}
			break;
		case READ_LABEL:
			if(prevState==READ_MACRO_ACTION){
				res=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			name.chars=malloc(prev);
			if(!name.chars){
				res=ERR_MEM;
				goto errorCleanup;
			}
			memcpy(name.chars,s,prev);
			name.len=prev;
			res=putLabel(prog,macroMap,name);
			if(res){
				goto  errorCleanup;
			}
			name.chars=NULL;//unlink to prevent double free
			name.len=0;
		break;
		case READ_MACRO_NAME:
			res=ERR_UNFINISHED_MACRO;
			goto errorCleanup;//unfinished macro
		}
	}
	if(false){
errorCleanup:
		assert(res!=0);
		freeMacro(tmpMacro);
		tmpMacro.actions=NULL;
		tmpMacro.len=0;
		free(name.chars);
		name.chars=NULL;
		name.len=0;
	}
	free(s);
	free(buffer);
	return res;
}


const char* typeToStr(ActionType t){
	switch(t){
		case INVALID:return "INVALID";
		case LOAD_INT:return "LOAD";
		case SWAP:return "SWAP";
		case LOAD:return "DEREF";
		case STORE:return "STORE";
		case JUMPIF:return "JUMPIF";
		case ROT:return "ROT";
		case FLIP:return "FLIP";

		case COMMENT_START:return "IGNORE_START";
		case INCLUDE:return "INCLUDE";
		case UNDEF:return "UNDEF";
		case LABEL_DEF:return "LABEL_DEF";
		case LABEL:return "LABEL";
		case MACRO_START:return "MACRO_START";
		case END:return "END";
	}
	assert(false&&"invalid value for ActionType");
	return NULL;
}

//reads a value from memory
//is an error occurs err is set to 1 otherwise err is set to 0
uint64_t memRead(ProgState* state,uint64_t* err){
	if(state->regA==LONG_IO_ADDR){
		*err=0;
		//TODO read long
		return 0;
	}else if(state->regA==CHAR_IO_ADDR){
		*err=0;
		unsigned char c=getchar();
		if((c&0x80)==0){
			return state->regA=c;
		}
		//TODO read UTF-8 char
		return 0;
	}else if(state->regA<MEM_SIZE){
		*err=0;
		return state->mem[state->regA];
	}
	*err=1;
	return 0;
}

//writes a value to memory
//is an error occurs the return value is 1 otherwise 0 is returned
uint64_t memWrite(ProgState* state){
	if(state->regB==LONG_IO_ADDR){
		printf("%"PRIx64"\n",state->regA);
		return 0;
	}else if(state->regB==CHAR_IO_ADDR){
		if(state->regA<0x80){
			putchar(state->regA);
		}
		//TODO write UTF-8 char
		return 0;
	}else if(state->regB<MEM_SIZE){
		state->mem[state->regB]=state->regA;
		return 0;
	}
	return 1;
}

int runProgram(Program prog,ProgState* state){
	uint64_t tmp;
	for(size_t ip=0;ip<prog.len;ip++){
		switch(prog.actions[ip].type){
			case INVALID://NOP
				break;
			case LABEL:
				return ERR_UNRESOLVED_LABEL;//unresolved label
			case INCLUDE:
			case COMMENT_START:
			case LABEL_DEF:
			case UNDEF:
			case MACRO_START:
			case END:
				return ERR_UNRESOLVED_MACRO;//unresolved macro/label (un)definition
			case LOAD_INT:
				state->regA=prog.actions[ip].data.asInt;
				break;
			case SWAP:
				tmp=state->regA;
				state->regA=state->regB;
				state->regB=tmp;
				break;
			case LOAD:
				state->regA=memRead(state,&tmp);
				if(tmp!=0){
					return tmp;
				}
				break;
			case STORE:
				tmp=memWrite(state);
				if(tmp!=0){
					return tmp;
				}
				break;
			case JUMPIF:
				if(state->regA&1){
					tmp=ip;
					ip=state->regB-1;//-1 to balance ip++
					state->regB=tmp+1;
				}
				break;
			case ROT://bit rotate by 1
				state->regA=state->regA>>1|state->regA<<63;
				break;
			case FLIP://flip lowest bit
				state->regA^=1;
				break;
		}
	}
	return NO_ERR;
}

int main(void) {
	char* filePath="./examples/test.mcl";//XXX get path from args
	Program prog={
			.actions=malloc(sizeof(Action)*INIT_CAP_PROG),
			.cap=INIT_CAP_PROG,
			.len=0
	};
	HashMap* macroMap=createHashMap(MAP_CAP);
	int res=include(&prog,macroMap,(String){.chars=filePath,.len=strlen(filePath)},
			(String){.len=0,.chars=NULL},0);
	switch(res){
	case ERR_MEM:
		fputs("Memory Error\n",stderr);
		return EXIT_FAILURE;
	case ERR_IO:
		fputs("IO Error\n",stderr);
		return EXIT_FAILURE;
	case ERR_FILE_NOT_FOUND:
		fputs("File not found\n",stderr);//TODO get Label of missing file
		return EXIT_FAILURE;

	case ERR_EXPANSION_OVERFLOW:
		fputs("Program exceeded expansion threshold\n",stderr);
		return EXIT_FAILURE;
    case ERR_UNRESOLVED_MACRO:
		fputs("Unresolved macro\n",stderr);
		return EXIT_FAILURE;
    case ERR_INVALID_IDENTIFER:
		fputs("Invalid Identifier\n",stderr);
		return EXIT_FAILURE;
    case ERR_UNEXPECTED_MACRO_DEF:
		fputs("Unexpected Macro definition\n",stderr);
		return EXIT_FAILURE;
    case ERR_UNEXPECTED_END:
		fputs("Unexpected #end statement\n",stderr);
		return EXIT_FAILURE;
    case ERR_UNFINISHED_IDENTIFER:
		fputs("Unfinished Identifier\n",stderr);
		return EXIT_FAILURE;
    case ERR_UNFINISHED_MACRO:
		fputs("Unfinished Macro\n",stderr);
		return EXIT_FAILURE;
    case ERR_UNFINISHED_COMMENT:
		fputs("Unfinished Comment\n",stderr);
		return EXIT_FAILURE;
    case ERR_UNRESOLVED_LABEL:
		fputs("Unresolved Label\n",stderr);
		return EXIT_FAILURE;
	case NO_ERR:
		res=freeHashMap(macroMap,&freeMapable);
		if(res){
			fputs("Unresolved Label\n",stderr);
			return EXIT_FAILURE;
		}
	}
	printf("compiled: %"PRIu64" actions \n",(uint64_t)prog.len);
	ProgState initState={
			.regA=0,
			.regB=0,
			.mem=malloc(MEM_SIZE*sizeof(uint64_t)),
			.memCap=MEM_SIZE
	};
	if(!initState.mem){
		fputs("Out of memory",stderr);
		return EXIT_FAILURE;
	}
	fflush(stdout);
	res=runProgram(prog,&initState);
	printf("\nexit value:%i",res);
	return EXIT_SUCCESS;
}
