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
#include <io.h>

#include "Structs.h"
#include "HashMap.h"
#include "Heap.h"

static const int MAX_DEPTH=128;

static const int MAP_CAP=2048;

static const int BUFFER_SIZE=2048;
static const int INIT_CAP_PROG=16;

static const char* LIB_DIR_NAME = "lib/";
static const char* DEFAULT_FILE_EXT = ".frs";//flipRot-script


typedef enum{
	READ_ACTION,//read actions
	READ_COMMENT,//read comment
	READ_LABEL,//read identifier of label
	READ_INCLUDE,//read until start of include-path
	READ_INCLUDE_MULTIWORD,//read name of include-path
	READ_MACRO_NAME,//read identifier of macro
	READ_MACRO_ACTION,//read actions in macro
	READ_UNDEF,//read identifier for undef
	READ_IFDEF,//read identifier for ifdef
	READ_IFNDEF,//read identifier for ifndef
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
			case ELSE:
			case ENDIF:
			case MACRO_START:
			case MACRO_END:
				break;//no pointer in data
			case INCLUDE:
			case IFDEF:
			case IFNDEF:
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

//test if cStr (NULL-terminated) is equals str
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
//XXX error reporting on failure
CodePos* makePosPtr(CodePos pos){
	CodePos* new=malloc(sizeof(CodePos));
	if(new){
		*new=pos;
	}
	return new;
}
static const CodePos NULL_POS={
		.file.chars="NULL",
		.file.len=4,
		.line=0,
		.posInLine=0,
};

Action loadAction(String name,CodePos pos){
	Action ret={.type=INVALID,.data.asInt=0,.at=makePosPtr(pos)};
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
	}else if(strCaseEq("#ifdef",name)){
		ret.type=IFDEF;
		ret.data.asString=(String){.len=0,.chars=NULL};
	}else if(strCaseEq("#ifndef",name)){
		ret.type=IFNDEF;
		ret.data.asString=(String){.len=0,.chars=NULL};
	}else if(strCaseEq("#else",name)){
		ret.type=ELSE;
	}else if(strCaseEq("#endif",name)){
		ret.type=ENDIF;
	}else if(strCaseEq("#undef",name)){
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
	// #flag -> compiler label for ifdef, cannot be dereferences
	// #ignore -> ignores int-overwrite error
	// #const -> integer constant
	// #break -> breakPoint
	return ret;
}

ErrorInfo readFile(FILE* file,Program* prog,HashMap* macroMap,String errPath,String filePath,int depth);

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
		ErrorInfo tmp=readFile(file,prog,macroMap,r.pos.at->file,(String){.chars=filePath,.len=pathLen},depth+1);
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
ErrorCode putLabel(Program* prog,CodePos pos,HashMap* map,String label){
	uint64_t target = jumpAddress(prog);
	Mapable prev=mapPut(map,label,
			(Mapable) { .type = MAPABLE_POS,.value.asPos = target });
	switch(prev.type){
	case MAPABLE_NONE:
		if(prev.value.asPos!=0){
			return prev.value.asPos;
		}
		break;
	case MAPABLE_POSARRAY:
		for(size_t i=0;i<prev.value.asPosArray.len;i++){
			assert(prog->actions[prev.value.asPosArray.data[i]].type==LABEL
					&&"Only type LABEL can be overwritten by labels");
			prog->actions[prev.value.asPosArray.data[i]].type=LOAD_INT;
			prog->actions[prev.value.asPosArray.data[i]].data.asInt=target;
		}
		freeMapable(prev);
		break;
	case MAPABLE_POS:
	case MAPABLE_MACRO:
		fprintf(stderr,"redefinition of label %.*s\n",(int)label.len,label.chars);
		freeMapable(prev);
		return ERR_LABEL_REDEF;
	}
	return NO_ERR;
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
	if(!ret.as.action.at){
		ret.isError=true;
		ret.as.error.errCode=ERR_MEM;
		ret.as.error.pos=pos;
	}
	return ret;
}

ErrorCode addAction(Program* prog,Action a){
	if(!a.at){
		return ERR_MEM;
	}
	//compress rot rot, flip flip and swap swap for efficiency
	if(prog->len>0){
		if(((prog->flags&PROG_FLAG_LABEL)==0)&&
				prog->actions[prog->len-1].type==a.type){
			switch(a.type){
			case FLIP:
			case SWAP:
				prog->len--;
				return NO_ERR;
			case ROT:
				prog->actions[prog->len-1].data.asInt+=a.data.asInt;
				if((prog->actions[prog->len-1].data.asInt&0x3f)==0){
					//remove NOP
					prog->len--;
				}
				return NO_ERR;
			//remaining ops are not compressible
			case LOAD_INT:
				//XXX flag: IGNORE_ERR_INT_OVERWRITE
				return ERR_INT_OVERWRITE;//two load-ints in sequence have no effect
			case INVALID:
			case LOAD:
			case STORE:
			case JUMPIF:
			case COMMENT_START:
			case IFDEF:
			case IFNDEF:
			case ELSE:
			case ENDIF:
			case UNDEF:
			case LABEL:
			case INCLUDE:
			case LABEL_DEF:
			case MACRO_START:
			case MACRO_END:
				break;
			}
		}else if(a.type==LOAD_INT){
			switch(prog->actions[prog->len-1].type){
			case FLIP:
			case ROT:
			case LOAD:
			case LABEL:
			case LOAD_INT://load-int overwrites changed value
				//XXX flag: IGNORE_ERR_INT_OVERWRITE
				return ERR_INT_OVERWRITE;
			default:
				break;
			}
		}
	}
	//ensure capacity
	if(prog->len>=prog->cap){
		Action* tmp=realloc(prog->actions,2*(prog->cap)*sizeof(Action));
		if(!tmp){
			return ERR_MEM;
		}
		prog->actions=tmp;
		(prog->cap)*=2;
	}
	prog->actions[prog->len++]=a;
	prog->flags&=~PROG_FLAG_LABEL;//clear flag label on add
	return NO_ERR;
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
		int if_count=0;
		uint64_t save=1;//lowest bit detects which branch is currently active
		for(size_t i=0;i<get.value.asMacro.len;i++){
			a=get.value.asMacro.actions[i];
			//don't override data in a.at
			a.at=createPos(*a.at,makePosPtr(pos));
			switch(a.type){
			case IFDEF:
			case IFNDEF:
				if(save&0x8000000000000000ULL){
					ret.isError=true;
					ret.as.error=(ErrorInfo){
						.errCode=ERR_IF_STACK_OVERFLOW,
						.pos=pos,
					};
					return ret;
				}
				if_count++;
				save<<=1;
				save|=((a.type==IFDEF)==
						(mapGet(macroMap,a.data.asString).type!=MAPABLE_NONE))?1:0;
				break;
			case ELSE:
				if(if_count==0){
					ret.isError=true;
					ret.as.error=(ErrorInfo){
						.errCode=ERR_UNEXPECTED_ELSE,
						.pos=pos,
					};
					return ret;
				}
				save^=1;
				break;
			case ENDIF:
				if(if_count==0){
					ret.isError=true;
					ret.as.error=(ErrorInfo){
						.errCode=ERR_UNEXPECTED_ENDIF,
						.pos=pos,
					};
					return ret;
				}
				if_count--;
				save>>=1;
				break;
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
			case UNDEF:
				if(save&1){
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
				}
				break;
			case LABEL_DEF:
				if(save&1){
					str=copyString(a.data.asString);
					if(str.len>0&&str.chars==NULL){
						ret.isError=true;
						ret.as.error=(ErrorInfo){
							.errCode=ERR_MEM,
							.pos=pos,
						};
						return ret;
					}
					ErrorCode err=putLabel(prog,pos,macroMap,str);
					if(err){
						ret.isError=true;
						ret.as.error=(ErrorInfo){
							.errCode=err,
							.pos=pos,
						};
						return ret;
					}
				}
				break;
			case INCLUDE:
				if(save&1){
					ErrorInfo r=include(prog,pos,macroMap,a.data.asString,
							filePath,depth+1);
					if(r.errCode){
						ret.isError=true;
						ret.as.error=r;
						return ret;
					}
				}
				break;
			case LABEL:
			if(save&1){
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
				if(aOe.as.action.type!=INVALID){
					ErrorCode res=addAction(prog,aOe.as.action);
					if(res){
						ret.isError=true;
						ret.as.error=(ErrorInfo){
							.errCode=res,
							.pos=pos,
						};
						return ret;
					}
				}
			}break;
			case LOAD_INT:
			case SWAP:
			case LOAD:
			case STORE:
			case JUMPIF:
			case ROT:
			case FLIP:
				if(save&1){
					ErrorCode res=addAction(prog,a);
					if(res){
						ret.isError=true;
						ret.as.error=(ErrorInfo){
							.errCode=res,
							.pos=pos,
						};
						return ret;
					}
				}
				break;
			}
		}
		if(if_count>0){
			ret.isError=true;
			ret.as.error.errCode=ERR_UNFINISHED_IF;
			ret.as.error.pos=pos;
			return ret;
		}
		ret.isError=false;
		ret.as.action=(Action){
			.type=INVALID,
			.data.asInt=0,
			.at=makePosPtr(pos),
		};
		if(!ret.as.action.at){
			ret.isError=true;
			ret.as.error.errCode=ERR_MEM;
			ret.as.error.pos=pos;
		}
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
ErrorInfo readFile(FILE* file,Program* prog,HashMap* macroMap,String errPath,String filePath,int depth){
	ErrorInfo err={.errCode=NO_ERR,
			.pos.file=errPath,
			.pos.line=1,
			.pos.posInLine=0,
			.pos.at=NULL,
	};
	if(depth>MAX_DEPTH){
		//exceeded maximum include depth
		err.errCode=ERR_EXPANSION_OVERFLOW;
		return err;
	}
	int if_count=0;
	uint64_t saveToken=1;
	size_t off,i,rem,prev=0,sCap=0;
	char* buffer=malloc(BUFFER_SIZE);
	char* s=NULL;
	if(!prog->actions){
		fputs("resetting prog",stderr);
		reinitMacro(prog);
	}
	if(!(buffer&&prog->actions&&macroMap)){
		err.errCode=ERR_MEM;
		goto errorCleanup;
	}
	ReadState state=READ_ACTION,prevState;
	Macro tmpMacro;
	tmpMacro.actions=NULL;
	tmpMacro.cap=0;
	String name,macroName;
	name.chars=NULL;
	if(!prog->actions){
		err.errCode=ERR_MEM;
		goto errorCleanup;
	}
	while(1){
		off=i=0;
		rem=fread(buffer,1,BUFFER_SIZE,file);
		if(rem==0){
			if(ferror(stdin)){
				err.errCode=ERR_IO;
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
					err.pos.line++;
					err.pos.posInLine=0;
				}
			}else {
				err.pos.posInLine++;
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
						err.errCode=ERR_MEM;
						goto errorCleanup;
					}
					t.chars++;
					t.len--;//remove quotation marks
					ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth);
					if(tmp.errCode){
						err=tmp;
						goto errorCleanup;
					}
					off=i+1;
					state=prevState;
				}
			}else if(isspace(buffer[i])){
				if(i>off||prev>0){
					size_t tmp=prev;//remember prev for transition to multi-word paths
					String t=getSegment(&prev,&s,&sCap,buffer,i,off);
					Action a=loadAction(t,err.pos);
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
									ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth);
									if(tmp.errCode){
										err=tmp;
										goto errorCleanup;
									}
									state=prevState;
								}else{
									prev=tmp;
									state=READ_INCLUDE_MULTIWORD;
								}
							}else{
								ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth);
								state=prevState;
								if(tmp.errCode){
									err=tmp;
									goto errorCleanup;
								}
							}
						}
						break;
					case READ_LABEL:
					case READ_IFDEF:
					case READ_IFNDEF:
					case READ_UNDEF:
					case READ_MACRO_NAME:
						if((t.len<1)||(loadAction(t,err.pos).type!=INVALID)){
							err.errCode=ERR_INVALID_IDENTIFER;
							goto errorCleanup;
						}
						name=copyString(t);
						if(name.len>0&&name.chars==NULL){
							err.errCode=ERR_MEM;
							goto errorCleanup;
						}
						switch(state){
						case READ_UNDEF:
							if(prevState==READ_ACTION){
								Mapable prev=mapPut(macroMap,name,
										(Mapable){.type=MAPABLE_NONE,.value.asPos=0});
								if(prev.type==MAPABLE_NONE&&prev.value.asPos!=0){
									err.errCode=prev.value.asPos;
									goto errorCleanup;
								}
							}else{
								err.errCode = addAction(&tmpMacro,(Action){.type=UNDEF,
									.data.asString=name,
									.at=makePosPtr(err.pos)});
								if(err.errCode){
									goto errorCleanup;
								}
							}
							name.chars=NULL;//unlink to prevent double free
							name.len=0;
							state=prevState;
							break;
						case READ_LABEL:
							if(prevState==READ_ACTION){
								err.errCode=putLabel(prog,err.pos,macroMap,name);
								if(err.errCode){
									goto errorCleanup;
								}
							}else{
								err.errCode=addAction(&tmpMacro,(Action){.type=LABEL_DEF,
									.data.asString=name,
									.at=makePosPtr(err.pos)});
								if(err.errCode){
									goto errorCleanup;
								}
							}
							name.chars=NULL;//unlink to prevent double free
							name.len=0;
							state=prevState;
							break;
						case READ_IFDEF:
						case READ_IFNDEF:
							if(prevState==READ_ACTION){
								if(saveToken&0x8000000000000000ULL){
									err.errCode=ERR_IF_STACK_OVERFLOW;
									goto errorCleanup;
								}
								if_count++;
								saveToken<<=1;
								saveToken|=((state==READ_IFDEF)==
										(mapGet(macroMap,name).type!=
												MAPABLE_NONE))?1:0;
							}else{
								err.errCode=addAction(&tmpMacro,(Action){
									.type=(state==READ_IFDEF)?IFDEF:IFNDEF,
										.data.asString=name,
										.at=makePosPtr(err.pos)});
								if(err.errCode){
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
							if(saveToken&1){
								prevState=state;
								state=READ_INCLUDE;
							}
							break;
						case IFDEF:
							prevState=state;
							state=READ_IFDEF;
							break;
						case IFNDEF:
							prevState=state;
							state=READ_IFNDEF;
							break;
						case ELSE:
							if(state==READ_ACTION){
								if(if_count==0){
									err.errCode=ERR_UNEXPECTED_ELSE;
									goto errorCleanup;//unexpected else
								}
								saveToken^=1;
							}else{
								err.errCode=addAction(&tmpMacro,(Action){
									.type=ELSE,
									.at=makePosPtr(err.pos)});
								if(err.errCode){
									goto errorCleanup;
								}
							}
							break;
						case ENDIF:
							if(state==READ_ACTION){
								if(if_count==0){
									err.errCode=ERR_UNEXPECTED_ENDIF;
									goto errorCleanup;//unexpected else
								}
								if_count--;
								saveToken>>=1;
							}else{
								err.errCode=addAction(&tmpMacro,(Action){
									.type=ENDIF,
									.at=makePosPtr(err.pos)});
								if(err.errCode){
									goto errorCleanup;
								}
							}
							break;
						case UNDEF:
							if(saveToken&1){
								prevState=state;
								state=READ_UNDEF;
							}
							break;
						case LABEL_DEF:
							if(saveToken&1){
								prevState=state;
								state=READ_LABEL;
							}
							break;
						case MACRO_START:
							if(saveToken&1){
								if(state==READ_ACTION){
									state=READ_MACRO_NAME;
									tmpMacro=(Macro){
										.cap=INIT_CAP_PROG,
										.len=0,
										.actions=malloc(INIT_CAP_PROG*sizeof(Action))
									};
									if(!tmpMacro.actions){
										err.errCode=ERR_MEM;
										goto errorCleanup;
									}
								}else{
									err.errCode=ERR_UNEXPECTED_MACRO_DEF;
									goto errorCleanup;
								}
							}break;
						case MACRO_END:
							if(saveToken&1){
								if(state==READ_MACRO_ACTION){
									if(defineMacro(macroMap,macroName,&tmpMacro)){
										err.errCode=ERR_MACRO_REDEF;
										unlinkMacro(&tmpMacro);//undef to prevent double free
										goto errorCleanup;
									}
									name.chars=NULL;//undef to prevent double free
									name.len=0;
									state=READ_ACTION;
								}else{
									err.errCode=ERR_UNEXPECTED_MACRO_END;
									goto errorCleanup;//unexpected end of macro
								}
							}break;
						case INVALID:
							if(saveToken&1){
								if(state==READ_ACTION){
									ActionOrError aOe=resolveLabel(prog,macroMap,err.pos,t,filePath,depth);
									if(aOe.isError){
										err=aOe.as.error;
										goto errorCleanup;
									}
									a=aOe.as.action;
								}else{
									a.type=LABEL;
									a.data.asString=copyString(t);
									if(a.data.asString.len>0&&!a.data.asString.chars){
										err.errCode=ERR_MEM;
										goto errorCleanup;
									}
									//fall through to add action
								}
								if(a.type==INVALID){
									break;//do not save label
								}
							}else{
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
							if(saveToken&1){
								if(state==READ_MACRO_ACTION){
									err.errCode=addAction(&tmpMacro,a);
									if(err.errCode){
										goto errorCleanup;
									}
								}else{
									err.errCode=addAction(prog,a);
									if(err.errCode){
										goto errorCleanup;
									}
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
					err.errCode=ERR_MEM;
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
		Action a=loadAction(t,err.pos);
		switch(state){
		case READ_ACTION:
			switch(a.type){
			case LABEL:
				assert(false&&"unreachable");//loadAction does not return LABEL
				break;
			case IFDEF:
			case IFNDEF:
			case ELSE://unfinished if-statement
				err.errCode=ERR_UNFINISHED_IF;
				goto errorCleanup;
				break;
			case ENDIF:
				if_count--;
				saveToken>>=1;
				break;
			case INCLUDE:
			case COMMENT_START://unfinished label/undef/comment
			case UNDEF:
			case LABEL_DEF:
				if(saveToken&1){
					err.errCode=ERR_UNFINISHED_IDENTIFER;
					goto errorCleanup;
				}break;
			case MACRO_START://unfinished macro
				if(saveToken&1){
					err.errCode=ERR_UNFINISHED_MACRO;
					goto errorCleanup;
				}break;
			case MACRO_END://unexpected end
				if(saveToken&1){
					err.errCode=ERR_UNEXPECTED_MACRO_END;
					goto errorCleanup;
				}break;
			case INVALID:
				if(saveToken&1){
					ActionOrError aOe=resolveLabel(prog,macroMap,err.pos,t,filePath,depth);
					if(aOe.isError){
						err=aOe.as.error;
						goto errorCleanup;
					}
					a=aOe.as.action;
					if(a.type==INVALID){
						break;
					}
				}else{
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
				if(saveToken&1){
					err.errCode=addAction(prog,a);
					if(err.errCode){
						goto errorCleanup;
					}
				}
				break;
			}
			break;
		case READ_COMMENT:
			if(prev<2||s[prev-1]!='#'||s[prev-2]!='_'){
				err.errCode=ERR_UNFINISHED_COMMENT;
				goto errorCleanup;
			}
			break;
		case READ_MACRO_ACTION:
			if(a.type!=MACRO_END){
				err.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;
			}
			if(defineMacro(macroMap,macroName,&tmpMacro)){
				err.errCode=ERR_MACRO_REDEF;
				unlinkMacro(&tmpMacro);//undef to prevent double free
				goto  errorCleanup;
			}
			name.chars=NULL;//unlink to prevent double free
			name.len=0;
			break;
		case READ_UNDEF:
			if(prevState==READ_MACRO_ACTION){
				err.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			Mapable prevVal=mapPut(macroMap,name,
					(Mapable){.type=MAPABLE_NONE,.value.asPos=0});
			if(prevVal.type==MAPABLE_NONE&&prevVal.value.asPos!=0){
				err.errCode=prevVal.value.asPos;
				goto errorCleanup;
			}
			name.chars=NULL;//unlink to prevent double free
			name.len=0;
			break;
		case READ_INCLUDE:
			if(prevState==READ_MACRO_ACTION){
				err.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			if(prev==0){
				err.errCode=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			}
			if(s[0]=='"'){
				if(s[prev-1]=='"'){
					t.chars++;
					t.len-=2;
					ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth);
					if(tmp.errCode){
						err=tmp;
						goto errorCleanup;
					}
				}else{
					err.errCode=ERR_UNFINISHED_IDENTIFER;
					goto errorCleanup;
				}
			}else{
				ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth);
				if(tmp.errCode){
					err=tmp;
					goto errorCleanup;
				}
			}
		break;
		case READ_INCLUDE_MULTIWORD:
			if(prevState==READ_MACRO_ACTION){
				err.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			if(prev<2){
				err.errCode=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			}
			if(s[prev-1]=='"'){
				t.chars++;
				t.len-=2;
				ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth);
				if(tmp.errCode){
					goto errorCleanup;
				}
			}else{
				err.errCode=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			}
			assert(0&&"unimplemented");
			break;
		case READ_LABEL:
			if(prevState==READ_MACRO_ACTION){
				err.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			name.chars=malloc(prev);
			if(!name.chars){
				err.errCode=ERR_MEM;
				goto errorCleanup;
			}
			memcpy(name.chars,s,prev);
			name.len=prev;
			err.errCode=putLabel(prog,err.pos,macroMap,name);
			if(err.errCode){
				goto  errorCleanup;
			}
			name.chars=NULL;//unlink to prevent double free
			name.len=0;
		break;
		case READ_MACRO_NAME:
			err.errCode=ERR_UNFINISHED_MACRO;
			goto errorCleanup;//unfinished macro
		case READ_IFDEF:
		case READ_IFNDEF:
			err.errCode=ERR_UNFINISHED_IF;
			goto errorCleanup;//unfinished if
		}
	}
	if(if_count>0){
		err.errCode=ERR_UNFINISHED_IF;
		goto errorCleanup;//unfinished if
	}
	if(false){
errorCleanup:
		assert(err.errCode!=0);
		freeMacro(&tmpMacro);
		free(name.chars);
		name.chars=NULL;
		name.len=0;
	}
	free(s);
	free(buffer);
	return err;
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
		case IFDEF:return "IFDEF";
		case IFNDEF:return "IFNDEF";
		case ELSE:return "ELSE";
		case ENDIF:return "ENDIF";
		case UNDEF:return "UNDEF";
		case LABEL_DEF:return "LABEL_DEF";
		case LABEL:return "LABEL";
		case MACRO_START:return "MACRO_START";
		case MACRO_END:return "END";
	}
	assert(false&&"invalid value for ActionType");
	return NULL;
}

uint64_t readWrapper(uint64_t fd,char* buffer,uint64_t* count){
	ssize_t numRead=read(fd,buffer,*count);
	if(numRead<0){
		*count=0;
		//XXX check read error codes
		return 1;
	}else{
		*count=numRead;
		return 0;
	}
}
uint64_t writeWrapper(uint64_t fd,char* buffer,uint64_t* count){
	ssize_t numRead=write(fd,buffer,*count);
	if(numRead<0){
		*count=0;
		//XXX check write error codes
		return 1;
	}else{
		*count=numRead;
		return 0;
	}
}

//reads a value from memory
//is an error occurs err is set to 1 otherwise err is set to 0
uint64_t memRead(ProgState* state,ErrorCode* err){
	if((state->regA&MEM_MASK_INVALID)!=0){
		if((state->regA&MEM_MASK_SYS)==MEM_MASK_SYS){
			*err=0;
			return state->sysReg[SYS_REG_COUNT-
								 (state->regA&SYS_ADDR_MASK)/sizeof(uint64_t)-1];
		}
	}else if((state->regA&MEM_MASK_STACK)==MEM_STACK_START){
		if(state->regA+sizeof(uint64_t)>MEM_SIZE){
			*err=ERR_HEAP_ILLEGAL_ACCESS;
			return 0;
		}
		*err=0;
		//XXX special handling for BigEndian systems
		return *((uint64_t*)(state->stackMem+(state->regA&STACK_ADDR_MASK)));
	}
	return heapRead(state->heap,state->regA,err);
}

//writes a value to memory
//is an error occurs the return value is 1 otherwise 0 is returned
ErrorCode memWrite(ProgState* state){
	if((state->regB&MEM_MASK_INVALID)!=0){
		if(state->regB==MEM_SYS_CALL){
			switch(state->regA){
			case CALL_RESIZE_HEAP:
				state->regA=heapEnsureCap(&state->heap,state->sysReg[1]);
				break;
			case CALL_READ:{
				char* buffer=malloc(state->REG_IO_COUNT);
				if(!buffer){
					return ERR_MEM;
				}
				state->regA=readWrapper(state->REG_IO_FD,buffer,
						state->PTR_REG_IO_COUNT);
				//TODO copyData to heap
				free(buffer);
			}break;
			case CALL_WRITE:{
				char* buffer=malloc(state->REG_IO_COUNT);
				if(!buffer){
					return ERR_MEM;
				}
				if(state->REG_IO_TARGET+state->REG_IO_COUNT>MEM_SIZE){
					return ERR_HEAP_ILLEGAL_ACCESS;
				}
				if(state->REG_IO_TARGET>=MEM_STACK_START){
					memcpy(buffer,((char*)state->stackMem)+state->REG_IO_TARGET-MEM_STACK_START
							,state->REG_IO_COUNT);
				}else if(state->REG_IO_TARGET+state->REG_IO_COUNT>=MEM_STACK_START){
					uint64_t off=MEM_STACK_START-state->REG_IO_TARGET;
					memcpy(buffer+off,state->stackMem,state->REG_IO_COUNT-off);
				}//no else
				if(state->REG_IO_TARGET<MEM_STACK_START){
					uint64_t count=MEM_STACK_START-state->REG_IO_TARGET;
					count=count>state->REG_IO_COUNT?state->REG_IO_COUNT:count;
					//TODO getData from heap
				}
				state->regA=writeWrapper(state->REG_IO_FD,buffer,
						state->PTR_REG_IO_COUNT);
				free(buffer);
			}break;
			default:
				state->regA=-1;//TODO errorCodes
				break;
			}
			return NO_ERR;
		}else if((state->regB&MEM_MASK_SYS)==MEM_MASK_SYS){
			state->sysReg[SYS_REG_COUNT-
						  (state->regB&SYS_ADDR_MASK)/sizeof(uint64_t)-1]=state->regA;
			return NO_ERR;
		}
		return ERR_HEAP_ILLEGAL_ACCESS;
	}else if((state->regB&MEM_MASK_STACK)==MEM_STACK_START){
		if(state->regB+sizeof(uint64_t)>MEM_SIZE){
			return ERR_HEAP_ILLEGAL_ACCESS;
		}
		//XXX special handling for BigEndian systems
		*((uint64_t*)(state->stackMem+(state->regB&STACK_ADDR_MASK)))=state->regA;
		return NO_ERR;
	}
	return heapWrite(state->heap,state->regB,state->regA);
}

//TODO? compile program to C?

ErrorInfo runProgram(Program prog,ProgState* state){
	ErrorCode ioRes;
	uint64_t tmp;
	for(size_t ip=0;ip<prog.len;){
		switch(prog.actions[ip].type){
			case LABEL:
				return (ErrorInfo){
					.errCode=ERR_UNRESOLVED_LABEL,
					.pos=prog.actions[ip].at?*prog.actions[ip].at:NULL_POS,
				};//unresolved label
			case INVALID:
				case INCLUDE:
			case COMMENT_START:
			case LABEL_DEF:
			case UNDEF:
			case IFDEF:
			case IFNDEF:
			case ELSE:
			case ENDIF:
			case MACRO_START:
			case MACRO_END:
				return (ErrorInfo){
					.errCode=ERR_UNRESOLVED_MACRO,
					.pos=prog.actions[ip].at?*prog.actions[ip].at:NULL_POS,
				};//unresolved macro/label (un)definition
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
				state->regA=memRead(state,&ioRes);
				if(ioRes){
					return (ErrorInfo){
						.errCode=ioRes,
						.pos=prog.actions[ip].at?*prog.actions[ip].at:NULL_POS,
					};
				}
				ip++;
				break;
			case STORE:
				ioRes=memWrite(state);
				if(ioRes){
					return (ErrorInfo){
						.errCode=ioRes,
						.pos=prog.actions[ip].at?*prog.actions[ip].at:NULL_POS,
					};
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
	return (ErrorInfo){
		.errCode=NO_ERR,
		.pos={{0}},
	};
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
	case ERR_IF_STACK_OVERFLOW:
		fputs("Program exceeded maximum nested if depth\n",stderr);
		break;
	case ERR_EXPANSION_OVERFLOW:
		fputs("Program exceeded macro-expansion threshold\n",stderr);
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
	case ERR_UNEXPECTED_ELSE:
		fputs("Unexpected #else statement\n",stderr);
		break;
	case ERR_UNEXPECTED_ENDIF:
		fputs("Unexpected #endif statement\n",stderr);
		break;
	case ERR_UNEXPECTED_MACRO_END:
		fputs("Unexpected #enddef statement\n",stderr);
		break;
	case ERR_UNFINISHED_IF:
		fputs("Unfinished if statement\n",stderr);
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
	case ERR_LABEL_REDEF:
		fputs("Label redefinition\n",stderr);
		break;
	case ERR_MACRO_REDEF:
		fputs("Macro redefinition\n",stderr);
		break;
	case ERR_INT_OVERWRITE:
		fputs("unused value is overwritten by load-int\n",stderr);
		break;
	case ERR_HEAP_ILLEGAL_ACCESS:
		fputs("Illegal Heap Access\n",stderr);
		break;
	case ERR_HEAP_OUT_OF_MEMORY:
		fputs("Heap out of Memory\n",stderr);
		break;
	case NO_ERR:
		return;
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


void printUsage(){
	puts("usage: <filename>");
	// -d <filename> => debug
	// -lib <path> => set lib path
}

int main(int argc,char** argv) {
	if(argc<2){
		puts("no file name provided");
		printUsage();
		return EXIT_FAILURE;
	}
	FILE* file;
	if(argc==2){
		file=fopen(argv[1],"r");;
		rootPath=getDirFromPath((String){.chars=argv[0],.len=strlen(argv[0])});
	}else{
		//TODO customize lib path
		puts("to many arguments");
		printUsage();
		return EXIT_FAILURE;
	}
	if(!file){
		fprintf(stderr,"File %s not found",argv[1]);
		return EXIT_FAILURE;
	}
	String filePath={.chars=argv[1],.len=strlen(argv[1])};
	//for debug purposes
	printf("root: %.*s\n",(int)rootPath.len,rootPath.chars);
	Program prog;
	reinitMacro(&prog);
	HashMap* macroMap=createHashMap(MAP_CAP);
	ErrorInfo res=readFile(file,&prog,macroMap,filePath,filePath,0);
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
			.stackMem = malloc(STACK_MEM_SIZE * sizeof(uint64_t)),
			.heap    = createHeap(HEAP_INIT_SIZE),
			.sysReg = {0},
	};
	if(!(initState.stackMem&&initState.heap.sections)){
		fputs("Out of memory",stderr);
		return EXIT_FAILURE;
	}
	fflush(stdout);
	res=runProgram(prog,&initState);
	if(res.errCode){
		fprintf(stderr,"Error while executing Program\n");
		printError(res);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
