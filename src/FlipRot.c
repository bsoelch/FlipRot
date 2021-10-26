/*
 ============================================================================
 Name        : FlipRot.c
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

static const char* LIB_DIR_NAME = "lib/";
static const char* DEFAULT_FILE_EXT = ".frs";//flipRot-script

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
	READ_INCLUDE_MULTIWORD,//read name of include-path
	READ_MACRO_NAME,//read name of macro
	READ_MACRO_ACTION,//read actions in macro
	READ_UNDEF,//read name for undef
} ReadState;

//directory of this executable
String rootPath;

void unlinkMacro(Macro* macro){
	macro->actions=NULL;
	macro->cap=macro->len=0;
}
void freeMacro(Macro* m){
	if(m->cap>0){
		for(size_t i=0;i<m->len;i++){
			switch(m->actions[i].type){
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
			case MACRO_END:
				break;//no pointer in data
			case INCLUDE:
			case UNDEF:
			case LABEL:
			case LABEL_DEF:
				free(m->actions[i].data.asString.chars);
			}
		}
		free(m->actions);
		unlinkMacro(m);
	}
}
void reinitMacro(Macro* macro){
	macro->actions=malloc(sizeof(Action)*INIT_CAP_PROG);
	macro->cap=INIT_CAP_PROG;
	macro->len=0;
}

//test if cStr (NULL-terminated) is equals to str (length len)
//ignores the case of all used chars
int strCaseEq(const char* cStr,String str){
	size_t i=0;
	for(;i<str.len;i++){
		if(toupper(cStr[i])!=toupper(str.chars[i])){
			return 0;
		}
	}
	return cStr[i]=='\0';
}
String copyString(String source){
	if(source.len==0){
		return (String){.len=0,.chars=NULL};
	}else{
		String str;
		str.len=source.len;
		str.chars=malloc(source.len);
		if(!str.chars){
			return str;
		}
		memcpy(str.chars,source.chars,source.len);
		str.len=source.len;
		return str;
	}
}
CodePos* createPos(CodePos pos,CodePos* prev){
	CodePos* new=malloc(sizeof(CodePos));
	if(new){
		*new=pos;
		new->at=prev;
	}
	return new;
}
CodePos* makePosPtr(CodePos pos){
	CodePos* new=malloc(sizeof(CodePos));
	if(new){
		*new=pos;
	}
	return new;
}

//TODO consistent handling of CodePos chains

Action loadAction(String name,CodePos pos){
	Action ret={.type=INVALID,.data.asInt=0,.at=makePosPtr(pos)};
	//XXX error if at==NULL
	char* tail=name.chars+name.len;
	long long ll=strtoull(name.chars,&tail,0);
	if(tail==name.chars+name.len){
		ret.type=LOAD_INT;
		ret.data.asInt=ll;
	}else if(strCaseEq("FLIP",name)){
		ret.type=FLIP;
		ret.data.asInt=1;
	}else if(strCaseEq("ROT",name)){
		ret.type=ROT;
		ret.data.asInt=1;
	}else if(strCaseEq("SWAP",name)){
		ret.type=SWAP;
		ret.data.asInt=1;
	}else if(strCaseEq("LOAD",name)){
		ret.type=LOAD;
	}else if(strCaseEq("STORE",name)){
		ret.type=STORE;
	}else if(strCaseEq("JUMPIF",name)){
		ret.type=JUMPIF;
	}else if(strCaseEq("#_",name)){//comment
		ret.type=COMMENT_START;
	}else if(strCaseEq("#include",name)){
		ret.type=INCLUDE;
		ret.data.asString=(String){.len=0,.chars=NULL};
	}else if(strCaseEq("#undef",name)){//TODO #ifdef, #ifndef, #else, #endif
		ret.type=UNDEF;
		ret.data.asString=(String){.len=0,.chars=NULL};
	}else if(strCaseEq("#label",name)){
		ret.type=LABEL_DEF;
		ret.data.asString=(String){.len=0,.chars=NULL};
	}else if(strCaseEq("#def",name)){
		ret.type=MACRO_START;
	}else if(strCaseEq("#enddef",name)){
		ret.type=MACRO_END;
	}
	return ret;
}

ErrorInfo readFile(FILE* file,Program* prog,HashMap* macroMap,String filePath,int depth);

static String getDirFromPath(String path){
	if(path.len>0){
		bool isFile=true;
		do{
			switch(path.chars[--path.len]){
			case '/'://XXX better handling of path separators
			case '\\':
			case ':':
				path.len++;
				isFile=false;
				break;
			}
		}while(isFile&&path.len>0);
	}
	return path;
}
bool hasFileExtension(String str){
	size_t i=str.len-1;
	do{
		switch(str.chars[i--]){
		case '.':
			return true;
		case '/'://XXX better handling of path separators
		case '\\':
		case ':':
			return false;
		}
	}while(i>0);//dot at position 0 is indicator for local includes
	return false;
}

ErrorInfo include(Program* prog,CodePos at,HashMap* macroMap,String path,String parentPath,int depth){
	bool hasExt=hasFileExtension(path);
	parentPath=getDirFromPath(parentPath);
	size_t pathLen=path.len+(parentPath.len>(rootPath.len+strlen(LIB_DIR_NAME))?
			parentPath.len:rootPath.len+strlen(LIB_DIR_NAME))+strlen(DEFAULT_FILE_EXT)+1;
	char* filePath=malloc(pathLen);
	ErrorInfo r=(ErrorInfo){
		.errCode=NO_ERR,
		.pos=at,
	};
	r.pos.at=makePosPtr((CodePos){
		.file=(String){.len=0,.chars=NULL},
		.line=0,
		.posInLine=0,
	});
	if(!(filePath&&r.pos.at)){
		r.errCode=ERR_MEM;
		return r;//out of memory
	}
	FILE* file;
	if(path.len>0&&path.chars[0]=='.'){//local file
		memcpy(filePath,parentPath.chars,parentPath.len);
		memcpy(filePath+parentPath.len,path.chars+1,path.len-1);
		pathLen=parentPath.len+path.len-1;
		r.pos.at->file=(String){.chars=filePath,.len=pathLen};
		if(!hasExt){
			memcpy(filePath+pathLen,DEFAULT_FILE_EXT,strlen(DEFAULT_FILE_EXT));
			pathLen+=strlen(DEFAULT_FILE_EXT);
		}
		filePath[pathLen]='\0';
		file=fopen(filePath,"r");
	}else{//try as lib path
		memcpy(filePath,rootPath.chars,rootPath.len);
		memcpy(filePath + rootPath.len,LIB_DIR_NAME,strlen(LIB_DIR_NAME));
		memcpy(filePath+rootPath.len+strlen(LIB_DIR_NAME),path.chars,path.len);
		pathLen=rootPath.len+strlen(LIB_DIR_NAME)+path.len;
		//don't include lib-path in codePos
		r.pos.at->file=(String){.chars=filePath+rootPath.len,.len=pathLen-rootPath.len};
		if(!hasExt){
			memcpy(filePath+pathLen,DEFAULT_FILE_EXT,strlen(DEFAULT_FILE_EXT));
			pathLen+=strlen(DEFAULT_FILE_EXT);
		}
		filePath[pathLen]='\0';
		file=fopen(filePath,"r");
		if(!file){//try as absolute path
			memcpy(filePath,path.chars,path.len);
			pathLen=path.len;
			r.pos.at->file=(String){.chars=filePath,.len=pathLen};
			if(!hasExt){
				memcpy(filePath+pathLen,DEFAULT_FILE_EXT,strlen(DEFAULT_FILE_EXT));
				pathLen+=strlen(DEFAULT_FILE_EXT);
			}
			filePath[pathLen]='\0';
			file=fopen(filePath,"r");
		}
	}
	if(file){
		ErrorInfo tmp=readFile(file,prog,macroMap,(String){.chars=filePath,.len=pathLen},depth+1);
		r.errCode=tmp.errCode;
		*r.pos.at=tmp.pos;
		fclose(file);
	}else{
		r.errCode=ERR_FILE_NOT_FOUND;
	}
	//don't free filePath (still in use as CodePos.file)
	return r;
}

size_t jumpAddress(Program* prog){
	//remember position as jump address
	prog->flags|=PROG_FLAG_LABEL;
	return prog->len;
}

//returns true if and error occurs
bool putLabel(Program* prog,CodePos pos,HashMap* map,String label){
	uint64_t target = jumpAddress(prog);
	Mapable prev=mapPut(map,label,
			(Mapable) { .type = MAPABLE_POS,.value.asPos = target });
	switch(prev.type){
	case MAPABLE_NONE:
		if(prev.value.asPos!=0){
			return true;
		}
		break;
	case MAPABLE_POSARRAY:
		for(size_t i=0;i<prev.value.asPosArray.len;i++){
			prog->actions[prev.value.asPosArray.data[i]].type=LOAD_INT;
			prog->actions[prev.value.asPosArray.data[i]].data.asInt=target;
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
	unlinkMacro(macro);
	return false;
}

ActionOrError addUnresolvedLabel(Mapable get,Program* prog,HashMap* map,CodePos pos,String label){
	ActionOrError ret;
	String str=copyString(label);
	if(str.len>0&&str.chars==NULL){
		ret.isError=true;
		ret.as.error=(ErrorInfo){
			.errCode=ERR_MEM,
			.pos=pos,
		};
		return ret;
	}
	get.value.asPosArray.data[get.value.asPosArray.len++]=jumpAddress(prog);
	Mapable res=mapPut(map,str,get);
	if(res.type==MAPABLE_NONE&&res.value.asPos!=0){
		ret.isError=true;
		ret.as.error=(ErrorInfo){
			.errCode=res.value.asPos,
			.pos=pos,
		};
		return ret;
	}
	//don't use label to prevent double free
	ret.isError=false;
	ret.as.action=(Action){
		.type=LABEL,
		.data.asString={.len=0,.chars=NULL},
		.at=makePosPtr(pos)
	};
	return ret;
}

bool addAction(Program* prog,Action a){
	if(!a.at){
		return false;
	}
	//compress rot rot, flip flip and swap swap for efficiency
	if(prog->len>0&&((prog->flags&PROG_FLAG_LABEL)==0)&&
			prog->actions[prog->len-1].type==a.type){
		switch(a.type){
		case FLIP:
		case SWAP:
			prog->len--;
			return true;
		case ROT:
			prog->actions[prog->len-1].data.asInt+=a.data.asInt;
			if((prog->actions[prog->len-1].data.asInt&0x3f)==0){
				//remove NOP
				prog->len--;
			}
			return true;
		//remaining ops are not compressible
		case INVALID:
		case LOAD_INT:
		case LOAD:
		case STORE:
		case JUMPIF:
		case COMMENT_START:
		case UNDEF:
		case LABEL:
		case INCLUDE:
		case LABEL_DEF:
		case MACRO_START:
		case MACRO_END:
			break;
		}
	}
	//ensure capacity
	if(prog->len>=prog->cap){
		Action* tmp=realloc(prog->actions,2*(prog->cap)*sizeof(Action));
		if(!tmp){
			return false;
		}
		prog->actions=tmp;
		(prog->cap)*=2;
	}
	prog->actions[prog->len++]=a;
	prog->flags&=~PROG_FLAG_LABEL;//clear flag label on add
	return true;
}

ActionOrError resolveLabel(Program* prog,HashMap* macroMap,CodePos pos,String label,String filePath,int depth){
	ActionOrError ret;
	if(depth>MAX_DEPTH){
		ret.isError=true;
		ret.as.error=(ErrorInfo){
			.errCode=ERR_EXPANSION_OVERFLOW,
			.pos=pos,
		};
		return ret;
	}
	Mapable get=mapGet(macroMap,label);
	switch(get.type){
	case MAPABLE_NONE://no break
		get.type=MAPABLE_POSARRAY;
		get.value.asPosArray.cap=INIT_CAP_PROG;
		get.value.asPosArray.len=0;
		get.value.asPosArray.data=malloc(INIT_CAP_PROG*sizeof(size_t));
		if(!get.value.asPosArray.data){
			ret.isError=true;
			ret.as.error=(ErrorInfo){
				.errCode=ERR_MEM,
				.pos=pos,
			};
			return ret;
		}
		return addUnresolvedLabel(get,prog,macroMap,pos,label);
	case MAPABLE_POSARRAY:
		if(get.value.asPosArray.len>=get.value.asPosArray.cap){
			size_t* tmp=realloc(get.value.asPosArray.data,
					2*get.value.asPosArray.cap*sizeof(size_t));
			if(!tmp){
				ret.isError=true;
				ret.as.error=(ErrorInfo){
					.errCode=ERR_MEM,
					.pos=pos,
				};
				return ret;
			}
			get.value.asPosArray.data=tmp;
			get.value.asPosArray.cap*=2;
		}
		return addUnresolvedLabel(get,prog,macroMap,pos,label);
	case MAPABLE_POS:
		ret.isError=false;
		ret.as.action=(Action){.type=LOAD_INT,
			.data.asInt=get.value.asPos,.at=makePosPtr(pos)};
		return ret;
	case MAPABLE_MACRO:;
		String str;
		Action a;
		for(size_t i=0;i<get.value.asMacro.len;i++){
			a=get.value.asMacro.actions[i];
			a.at=createPos(*a.at,makePosPtr(pos));
			switch(a.type){
			case INVALID://invalid action
			case COMMENT_START:
			case MACRO_START:
			case MACRO_END:
				ret.isError=true;
				ret.as.error=(ErrorInfo){
					.errCode=ERR_UNRESOLVED_MACRO,
					.pos=pos,
				};
				return ret;
			case UNDEF:;
				Mapable prev=mapPut(macroMap,a.data.asString,
						(Mapable){.type=MAPABLE_NONE,.value.asPos=0});
				if(prev.type==MAPABLE_NONE&&prev.value.asPos!=0){
					ret.isError=true;
					ret.as.error=(ErrorInfo){
						.errCode=prev.value.asPos,
						.pos=pos,
					};
					return ret;
				}
				break;
			case LABEL_DEF:
				str=copyString(a.data.asString);
				if(str.len>0&&str.chars==NULL){
					ret.isError=true;
					ret.as.error=(ErrorInfo){
						.errCode=ERR_MEM,
						.pos=pos,
					};
					return ret;
				}
				int err=putLabel(prog,pos,macroMap,str);
				if(err){
					ret.isError=true;
					ret.as.error=(ErrorInfo){
						.errCode=err,
						.pos=pos,
					};
					return ret;
				}
				break;
			case INCLUDE:;
				ErrorInfo r=include(prog,pos,macroMap,a.data.asString,
						filePath,depth+1);
				if(r.errCode){
					ret.isError=true;
					ret.as.error=r;
					return ret;
				}
				break;
			case LABEL:{
				str=copyString(a.data.asString);
				if(str.len>0&&str.chars==NULL){
					ret.isError=true;
					ret.as.error=(ErrorInfo){
						.errCode=ERR_MEM,
						.pos=pos,
					};
					return ret;
				}
				ActionOrError aOe=resolveLabel(prog,macroMap,a.at?*a.at:pos,str,filePath,depth+1);
				if(aOe.isError){
					return aOe;
				}
				if(!addAction(prog,aOe.as.action)){
					ret.isError=true;
					ret.as.error=(ErrorInfo){
						.errCode=ERR_MEM,
						.pos=pos,
					};
					return ret;
				}
			}break;
			case LOAD_INT:
			case SWAP:
			case LOAD:
			case STORE:
			case JUMPIF:
			case ROT:
			case FLIP:
				if(!addAction(prog,a)){
					ret.isError=true;
					ret.as.error=(ErrorInfo){
						.errCode=ERR_MEM,
						.pos=pos,
					};
					return ret;
				}
				break;
			}
		}
		ret.isError=false;
		ret.as.action=(Action){
			.type=INVALID,
			.data.asInt=0,
			.at=makePosPtr(pos),
		};
		return ret;
		break;
	}
	assert(false&&"unreachable");
	ret.isError=true;
	ret.as.error=(ErrorInfo){
		.errCode=NO_ERR,
		.pos=(CodePos){{0}},//values are irrelevant
	};
	return ret;
}

String getSegment(size_t* prev,char** s,size_t* sCap,char* buffer,size_t i,size_t off){
	String t={.len=(*prev)+i-off,.chars=NULL};
	if((*prev)>0){
		if((*prev)+i-off>(*sCap)){
			t.chars=realloc((*s),(*prev)+i-off);
			if(!t.chars){
				return t;
			}
			(*sCap)=(*prev)+i-off;
			(*s)=t.chars;
		}
		memcpy((*s)+(*prev),buffer+off,i-off);
		t.chars=(*s);
		(*prev)=0;
	}else{
		t.chars=buffer+off;
	}
	return t;
}


//XXX allow definition of macros in macros
ErrorInfo readFile(FILE* file,Program* prog,HashMap* macroMap,String filePath,int depth){
	ErrorInfo res={.errCode=NO_ERR,
			.pos.file=filePath,
			.pos.line=1,
			.pos.posInLine=0,
			.pos.at=NULL,
	};
	//FIXME prevent overwriting of res
	if(depth>MAX_DEPTH){
		//exceeded maximum include depth
		res.errCode=ERR_EXPANSION_OVERFLOW;
		return res;
	}
	size_t off,i,rem,prev=0,sCap=0;
	char* buffer=malloc(BUFFER_SIZE);
	char* s=NULL;
	if(!prog->actions){
		fputs("resetting prog",stderr);
		reinitMacro(prog);
	}
	if(!(buffer&&prog->actions&&macroMap)){
		res.errCode=ERR_MEM;
		goto errorCleanup;
	}
	ReadState state=READ_ACTION,prevState;
	Macro tmpMacro;//XXX? stack
	tmpMacro.actions=NULL;
	tmpMacro.cap=0;
	String name,macroName;
	name.chars=NULL;
	if(!prog->actions){
		res.errCode=ERR_MEM;
		goto errorCleanup;
	}
	while(1){
		off=i=0;
		rem=fread(buffer,1,BUFFER_SIZE,file);
		if(rem==0){
			if(ferror(stdin)){
				res.errCode=ERR_IO;
				goto errorCleanup;
			}else{
				break;
			}
		}
		while(rem>0){
			if((buffer[i]=='\r')||buffer[i]=='\n'){
				// \r or \n
				if((buffer[i]=='\r'||((i>0&&buffer[i-1]!='\r')||
							(i==0&&(prev==0||s[prev-1]!='\r'))))){//not \r\n
					res.pos.line++;
					res.pos.posInLine=0;
				}
			}else {
				res.pos.posInLine++;
			}
			if(state==READ_COMMENT){
				if(off<=i-1&&buffer[i-1]=='_'&&buffer[i]=='#'){
					prev=0;//clear buffered characters
					off=i+1;
					state=prevState;
				}
			}else if(state==READ_INCLUDE_MULTIWORD){
				if(buffer[i]=='"'){
					String t=getSegment(&prev,&s,&sCap,buffer,i,off);
					if(!t.chars){
						res.errCode=ERR_MEM;
						goto errorCleanup;
					}
					t.chars++;
					t.len--;//remove quotation marks
					ErrorInfo tmp=include(prog,res.pos,macroMap,t,filePath,depth);
					if(tmp.errCode){
						res=tmp;
						goto errorCleanup;
					}
					off=i+1;
					state=prevState;
				}
			}else if(isspace(buffer[i])){
				if(i>off||prev>0){
					size_t tmp=prev;//remember prev for transition to multi-word paths
					String t=getSegment(&prev,&s,&sCap,buffer,i,off);
					Action a=loadAction(t,res.pos);
					switch(state){
					case READ_COMMENT:
					case READ_INCLUDE_MULTIWORD:
						assert(0&&"unreachable");
						break;
					case READ_INCLUDE:
						if(t.len>0){
							if(t.chars[0]=='"'){
								if(t.chars[t.len-1]=='"'){
									t.chars++;
									t.len-=2;//remove quotation marks
									ErrorInfo tmp=include(prog,res.pos,macroMap,t,filePath,depth);
									if(tmp.errCode){
										res=tmp;
										goto errorCleanup;
									}
									state=prevState;
								}else{
									prev=tmp;
									state=READ_INCLUDE_MULTIWORD;
								}
							}else{
								ErrorInfo tmp=include(prog,res.pos,macroMap,t,filePath,depth);
								state=prevState;
								if(tmp.errCode){
									res=tmp;
									goto errorCleanup;
								}
							}
						}
						break;
					case READ_LABEL:
					case READ_UNDEF:
					case READ_MACRO_NAME:
						if((t.len<1)||(loadAction(t,res.pos).type!=INVALID)){
							res.errCode=ERR_INVALID_IDENTIFER;
							goto errorCleanup;
						}
						name=copyString(t);
						if(name.len>0&&name.chars==NULL){
							res.errCode=ERR_MEM;
							goto errorCleanup;
						}
						switch(state){
						case READ_UNDEF:
							if(prevState==READ_ACTION){
								Mapable prev=mapPut(macroMap,name,
										(Mapable){.type=MAPABLE_NONE,.value.asPos=0});
								if(prev.type==MAPABLE_NONE&&prev.value.asPos!=0){
									res.errCode=prev.value.asPos;
									goto errorCleanup;
								}
							}else{
								if(!addAction(&tmpMacro,(Action){.type=UNDEF,
										.data.asString=name,
										.at=makePosPtr(res.pos)})){
									res.errCode=ERR_MEM;
									goto errorCleanup;
								}
							}
							name.chars=NULL;//unlink to prevent double free
							name.len=0;
							state=prevState;
							break;
						case READ_LABEL:
							if(prevState==READ_ACTION){
								if(putLabel(prog,res.pos,macroMap,name)){
									res.errCode=ERR_MEM;
									goto errorCleanup;
								}
							}else{
								if(!addAction(&tmpMacro,(Action){.type=LABEL_DEF,
										.data.asString=name,
										.at=makePosPtr(res.pos)})){
									res.errCode=ERR_MEM;
									goto errorCleanup;
								}
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
									res.errCode=ERR_MEM;
									goto errorCleanup;
								}
							}else{
								res.errCode=ERR_UNEXPECTED_MACRO_DEF;
								goto errorCleanup;
							}
							break;
						case MACRO_END:
							if(state==READ_MACRO_ACTION){
								if(defineMacro(macroMap,macroName,&tmpMacro)){
									res.errCode=ERR_MACRO_REDEF;
									unlinkMacro(&tmpMacro);//undef to prevent double free
									goto errorCleanup;
								}
								name.chars=NULL;//undef to prevent double free
								name.len=0;
								state=READ_ACTION;
							}else{
								res.errCode=ERR_UNEXPECTED_END;
								goto errorCleanup;//unexpected end of macro
							}
							break;
						case INVALID:
							if(state==READ_ACTION){
								ActionOrError aOe=resolveLabel(prog,macroMap,res.pos,t,filePath,depth);
								if(aOe.isError){
									res=aOe.as.error;
									goto errorCleanup;
								}
								a=aOe.as.action;
							}else{
								a.type=LABEL;
								a.data.asString=copyString(t);
								if(a.data.asString.len>0&&!a.data.asString.chars){
									res.errCode=ERR_MEM;
									goto errorCleanup;
								}
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
								if(!addAction(&tmpMacro,a)){
									res.errCode=ERR_MEM;
									goto errorCleanup;
								}
							}else{
								if(!addAction(prog,a)){
									res.errCode=ERR_MEM;
									goto errorCleanup;
								}
							}
						}
					}

				}
				if(state!=READ_INCLUDE_MULTIWORD){
					off=i+1;
				}
			}
			i++;
			rem--;
		}
		if(i>off){
			if (prev+i-off>sCap) {
				char *t = realloc(s, prev + i - off);
				if (!t) {
					res.errCode=ERR_MEM;
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
		String t=(String){.chars=s,.len=prev};
		Action a=loadAction(t,res.pos);
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
				res.errCode=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			case MACRO_START://unfinished macro
				res.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;
			case MACRO_END://unexpected end
				res.errCode=ERR_UNEXPECTED_END;
				goto errorCleanup;
			case INVALID:;
				ActionOrError aOe=resolveLabel(prog,macroMap,res.pos,t,filePath,depth);
				if(aOe.isError){
					res=aOe.as.error;
					goto errorCleanup;
				}
				a=aOe.as.action;
				if(a.type==INVALID){
					break;
				}
			//no break
			case LOAD_INT:
			case SWAP:
			case LOAD:
			case STORE:
			case JUMPIF:
			case ROT:
			case FLIP:
				if(!addAction(prog,a)){
					res.errCode=ERR_MEM;
					goto errorCleanup;
				}
				break;
			}
			break;
		case READ_COMMENT:
			if(prev<2||s[prev-1]!='#'||s[prev-2]!='_'){
				res.errCode=ERR_UNFINISHED_COMMENT;
				goto errorCleanup;
			}
			break;
		case READ_MACRO_ACTION:
			if(a.type!=MACRO_END){
				res.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;
			}
			if(defineMacro(macroMap,macroName,&tmpMacro)){
				res.errCode=ERR_MACRO_REDEF;
				unlinkMacro(&tmpMacro);//undef to prevent double free
				goto  errorCleanup;
			}
			name.chars=NULL;//unlink to prevent double free
			name.len=0;
			break;
		case READ_UNDEF:
			if(prevState==READ_MACRO_ACTION){
				res.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			Mapable prevVal=mapPut(macroMap,name,
					(Mapable){.type=MAPABLE_NONE,.value.asPos=0});
			if(prevVal.type==MAPABLE_NONE&&prevVal.value.asPos!=0){
				res.errCode=prevVal.value.asPos;
				goto errorCleanup;
			}
			name.chars=NULL;//unlink to prevent double free
			name.len=0;
			break;
		case READ_INCLUDE:
			if(prevState==READ_MACRO_ACTION){
				res.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			if(prev==0){
				res.errCode=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			}
			if(s[0]=='"'){
				if(s[prev-1]=='"'){
					t.chars++;
					t.len-=2;
					ErrorInfo tmp=include(prog,res.pos,macroMap,t,filePath,depth);
					if(tmp.errCode){
						res=tmp;
						goto errorCleanup;
					}
				}else{
					res.errCode=ERR_UNFINISHED_IDENTIFER;
					goto errorCleanup;
				}
			}else{
				ErrorInfo tmp=include(prog,res.pos,macroMap,t,filePath,depth);
				if(tmp.errCode){
					res=tmp;
					goto errorCleanup;
				}
			}
		break;
		case READ_INCLUDE_MULTIWORD:
			if(prevState==READ_MACRO_ACTION){
				res.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			if(prev<2){
				res.errCode=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			}
			if(s[prev-1]=='"'){
				t.chars++;
				t.len-=2;
				ErrorInfo tmp=include(prog,res.pos,macroMap,t,filePath,depth);
				if(tmp.errCode){
					goto errorCleanup;
				}
			}else{
				res.errCode=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			}
			assert(0&&"unimplemented");
			break;
		case READ_LABEL:
			if(prevState==READ_MACRO_ACTION){
				res.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			name.chars=malloc(prev);
			if(!name.chars){
				res.errCode=ERR_MEM;
				goto errorCleanup;
			}
			memcpy(name.chars,s,prev);
			name.len=prev;
			if(putLabel(prog,res.pos,macroMap,name)){
				res.errCode=ERR_MEM;
				goto  errorCleanup;
			}
			name.chars=NULL;//unlink to prevent double free
			name.len=0;
		break;
		case READ_MACRO_NAME:
			res.errCode=ERR_UNFINISHED_MACRO;
			goto errorCleanup;//unfinished macro
		}
	}
	if(false){
errorCleanup:
		assert(res.errCode!=0);
		freeMacro(&tmpMacro);
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
		case MACRO_END:return "END";
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
	for(size_t ip=0;ip<prog.len;){
		switch(prog.actions[ip].type){
			case INVALID://NOP
				ip++;
				break;
			case LABEL:
				return ERR_UNRESOLVED_LABEL;//unresolved label
			case INCLUDE:
			case COMMENT_START:
			case LABEL_DEF:
			case UNDEF:
			case MACRO_START:
			case MACRO_END:
				return ERR_UNRESOLVED_MACRO;//unresolved macro/label (un)definition
			case FLIP://flip lowest bit
				state->regA^=1;
				ip++;
				break;
			case ROT:;//bit rotation
				int count=0x3f&(prog.actions[ip].data.asInt);
				state->regA=(state->regA>>count)|(state->regA<<(64-count));
				ip++;
				break;
			case SWAP:
				tmp=state->regA;
				state->regA=state->regB;
				state->regB=tmp;
				ip++;
				break;
			case LOAD_INT:
				state->regA=prog.actions[ip].data.asInt;
				ip++;
				break;
			case LOAD:
				state->regA=memRead(state,&tmp);
				if(tmp!=0){
					return tmp;
				}
				ip++;
				break;
			case STORE:
				tmp=memWrite(state);
				if(tmp!=0){
					return tmp;
				}
				ip++;
				break;
			case JUMPIF:
				if(state->regA&1){
					tmp=ip+1;
					ip=state->regB;
					state->regB=tmp;
				}else{
					ip++;
				}
				break;
		}
	}
	return NO_ERR;
}

void printError(ErrorInfo err){
	switch(err.errCode){
	case ERR_MEM:
		fputs("Memory Error\n",stderr);
		break;
	case ERR_IO:
		fputs("IO Error\n",stderr);
		break;
	case ERR_FILE_NOT_FOUND:
		fputs("File not found\n",stderr);
		break;
	case ERR_EXPANSION_OVERFLOW:
		fputs("Program exceeded expansion threshold\n",stderr);
		break;
	case ERR_UNRESOLVED_MACRO:
		fputs("Unresolved macro\n",stderr);
		break;
	case ERR_INVALID_IDENTIFER:
		fputs("Invalid Identifier\n",stderr);
		break;
	case ERR_UNEXPECTED_MACRO_DEF:
		fputs("Unexpected Macro definition\n",stderr);
		break;
	case ERR_UNEXPECTED_END:
		fputs("Unexpected #end statement\n",stderr);
		break;
	case ERR_UNFINISHED_IDENTIFER:
		fputs("Unfinished Identifier\n",stderr);
		break;
	case ERR_UNFINISHED_MACRO:
		fputs("Unfinished Macro\n",stderr);
		break;
	case ERR_UNFINISHED_COMMENT:
		fputs("Unfinished Comment\n",stderr);
		break;
	case ERR_UNRESOLVED_LABEL:
		fputs("Unresolved Label\n",stderr);
		break;
	case ERR_MACRO_REDEF:
		fputs("Macro redefinition\n",stderr);
		break;
	case NO_ERR:
		return;
	default:
		assert(0&&"unreachable");
	}
	CodePos pos=err.pos;
	while(1){
		fprintf(stderr,"at line:%"PRIu64", pos:%"PRIu64" in %.*s\n",
						(uint64_t)pos.line,(uint64_t)pos.posInLine,
						(int)pos.file.len,pos.file.chars);
		if(!pos.at)
			break;
		pos=(*pos.at);
	}
}

int main(int argc,char** argv) {
	if(argc<2){
		puts("no file name provided");
		puts("usage: <filename>");
		return EXIT_FAILURE;
	}
	FILE* file=fopen(argv[1],"r");
	if(!file){
		fprintf(stderr,"File %s not found",argv[1]);
		return EXIT_FAILURE;
	}
	rootPath=getDirFromPath((String){.chars=argv[0],.len=strlen(argv[0])});
	String filePath={.chars=argv[1],.len=strlen(argv[1])};
	//for debug purposes
	printf("root: %.*s\n",(int)rootPath.len,rootPath.chars);
	Program prog;
	reinitMacro(&prog);
	HashMap* macroMap=createHashMap(MAP_CAP);
	ErrorInfo res=readFile(file,&prog,macroMap,filePath,0);
	fclose(file);
	if(res.errCode){
		printError(res);
		return EXIT_FAILURE;
	}
	//XXX? also return the matching label
	size_t addr=freeHashMap(macroMap,&freeMapable);
	if(addr<SIZE_MAX){
		res.errCode=ERR_UNRESOLVED_LABEL;
		if(addr<prog.len&&prog.actions[addr].at){
			res.pos=*prog.actions[addr].at;
		}else{
			res.errCode=ERR_MEM;
			res.pos.file=filePath;
			res.pos.line=SIZE_MAX;
			res.pos.posInLine=0;
			res.pos.at=NULL;
		}
		printError(res);
		return EXIT_FAILURE;
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
	int exitCode=runProgram(prog,&initState);
	printf("\nexit value:%i",exitCode);
	return EXIT_SUCCESS;
}
