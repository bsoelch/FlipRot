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
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "Structs.h"
#include "HashMap.h"
#include "Heap.h"

static const int MAX_DEPTH=128;

static const int MAP_CAP=2048;

static const int BUFFER_SIZE=2048;
static const int INIT_CAP_PROG=16;

static const int READ_DEBUG_INIT_CAP=128;

static const char* DEF_LIB_DIR_NAME = "lib/";
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
	READ_BREAKPOINT,//read label for breakpoint
} ReadState;

typedef enum{
	READ_COMMAND,
	READ_STEP_COUNT,
	READ_ENABLE_ID,
	READ_DISABLE_ID,
	READ_BREAK_AT_ID,
	READ_MEM_NAME,
	READ_MEM_OFF,
	READ_MEM_LEN,
	READ_UNREG_NAME,
}DebugReadState;

//directory of this executable
String libPath;

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
			case SYSTEM:
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
			case BREAKPOINT:
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

static int digitFromChar(char c){
	if(c>='0'&&c<='9'){
		return c-'0';
	}else if(c>='A'&&c<='Z'){
		return c-'A'+10;
	}else if(c>='a'&&c<='z'){
		return c-'a'+10;
	}else{
		return -1;
	}
}
uint64_t u64fromStr(String str,bool* isU64){
	if(str.len==0){
		*isU64=false;
		return 0;
	}
	uint64_t ret=0;
	int off=0,base=10,d;
	if(str.len>1&&str.chars[0]=='0'){
		switch(str.chars[1]){
		case 'X':
		case 'x':
			base=16;
			off=2;
			break;
		case 'B':
		case 'b':
			base=2;
			off=2;
			break;
		}
	}// #<hex> conflicts with #def
	if(off>=str.len){
		*isU64=false;
		return 0;
	}
	//value of overflow protection
	uint64_t maxVal=UINT64_MAX/base;
	for(int i=off;i<str.len;i++){
		d=digitFromChar(str.chars[i]);
		if(d<0||d>=base){
			*isU64=false;
			return 0;
		}
		if(ret>maxVal){
			*isU64=false;
			return 0;
		}
		ret*=base;
		ret+=d;
	}
	*isU64=true;
	return ret;
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
	bool isU64=false;
	uint64_t ll=u64fromStr(name,&isU64);
	if(isU64){
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
	}else if(strCaseEq("SYS",name)){
		ret.type=SYSTEM;
	}else if(strCaseEq("#breakpoint",name)){//breakpoint
		ret.type=BREAKPOINT;
		ret.data.asString=(String){.len=0,.chars=NULL};
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
	//XXX compiler commands:
	// #flag -> compiler label for ifdef, cannot be dereferenced
	// #ignore -> ignores int-overwrite error
	// #const -> integer constant
	return ret;
}

ErrorInfo readFile(FILE* file,Program* prog,HashMap* macroMap,
		String errPath,String filePath,int depth,bool debug);

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

ErrorInfo include(Program* prog,CodePos at,HashMap* macroMap,
		String path,String parentPath,int depth,bool debug){
	bool hasExt=hasFileExtension(path);
	parentPath=getDirFromPath(parentPath);
	size_t pathLen=path.len+(parentPath.len>(libPath.len)?
			parentPath.len:libPath.len)+strlen(DEFAULT_FILE_EXT)+1;
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
		memcpy(filePath,libPath.chars,libPath.len);
		memcpy(filePath+libPath.len,path.chars,path.len);
		pathLen=libPath.len+path.len;
		//don't include lib-path in codePos
		r.pos.at->file=(String){.chars=filePath+libPath.len,.len=pathLen-libPath.len};
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
		ErrorInfo tmp=readFile(file,prog,macroMap,r.pos.at->file,
				(String){.chars=filePath,.len=pathLen},depth+1,debug);
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
	size_t target = jumpAddress(prog);
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

ErrorCode addAction(Program* prog,Action a,bool debugMode){
	if(!a.at){
		return ERR_MEM;
	}
	if(a.type==BREAKPOINT&&!debugMode){
		return NO_ERR;//skip breakpoints if not in debug mode
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
			case STORE:
				//TODO? errorMessage
				return NO_ERR;//two stores with same data have no effect
			//remaining ops are not compressible
			case LOAD_INT:
				//XXX flag: IGNORE_ERR_INT_OVERWRITE
				return ERR_INT_OVERWRITE;//two load-ints in sequence have no effect
			case INVALID:
			case LOAD:
			case SYSTEM:
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
			case BREAKPOINT:
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

ActionOrError resolveLabel(Program* prog,HashMap* macroMap,CodePos pos,String label,String filePath,int depth,bool debug){
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
							filePath,depth+1,debug);
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
				ActionOrError aOe=resolveLabel(prog,macroMap,a.at?*a.at:pos,str,filePath,depth+1,debug);
				if(aOe.isError){
					return aOe;
				}
				if(aOe.as.action.type!=INVALID){
					ErrorCode res=addAction(prog,aOe.as.action,debug);
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
			case BREAKPOINT:
			case SYSTEM:
				if(save&1){
					ErrorCode res=addAction(prog,a,debug);
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

ErrorInfo readFile(FILE* file,Program* prog,HashMap* macroMap,
		String errPath,String filePath,int depth,bool debug){
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
					ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth,debug);
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
									ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth,debug);
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
								ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth,debug);
								state=prevState;
								if(tmp.errCode){
									err=tmp;
									goto errorCleanup;
								}
							}
						}
						break;
					case READ_BREAKPOINT:
					case READ_LABEL:
					case READ_IFDEF:
					case READ_IFNDEF:
					case READ_UNDEF:
					case READ_MACRO_NAME:
						if((t.len<1)||(a.type!=INVALID)){
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
									.at=makePosPtr(err.pos)},debug);
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
									.at=makePosPtr(err.pos)},debug);
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
										.at=makePosPtr(err.pos)},debug);
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
						case READ_BREAKPOINT:;
							Action a=(Action){.type=BREAKPOINT,
								.data.asString=name,
								.at=makePosPtr(err.pos)};
							if(prevState==READ_ACTION){
								err.errCode = addAction(prog,a,debug);
							}else{
								err.errCode = addAction(&tmpMacro,a,debug);
							}
							if(err.errCode){
								goto errorCleanup;
							}
							name.chars=NULL;//unlink to prevent double free
							name.len=0;
							state=prevState;
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
									.at=makePosPtr(err.pos)},debug);
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
									.at=makePosPtr(err.pos)},debug);
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
						case BREAKPOINT:
							if(saveToken&1){
								prevState=state;
								state=READ_BREAKPOINT;
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
									ActionOrError aOe=resolveLabel(prog,macroMap,err.pos,t,filePath,depth,debug);
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
						case SYSTEM:
							if(saveToken&1){
								if(state==READ_MACRO_ACTION){
									err.errCode=addAction(&tmpMacro,a,debug);
									if(err.errCode){
										goto errorCleanup;
									}
								}else{
									err.errCode=addAction(prog,a,debug);
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
			case COMMENT_START:
			case INCLUDE://unfinished identifier for macro/label/undef/break
			case UNDEF:
			case LABEL_DEF:
			case BREAKPOINT:
			case MACRO_START:
				if(saveToken&1){
					err.errCode=ERR_UNFINISHED_IDENTIFER;
					goto errorCleanup;
				}break;//unfinished macro
			case MACRO_END://unexpected end
				if(saveToken&1){
					err.errCode=ERR_UNEXPECTED_MACRO_END;
					goto errorCleanup;
				}break;
			case INVALID:
				if(saveToken&1){
					ActionOrError aOe=resolveLabel(prog,macroMap,err.pos,t,filePath,depth,debug);
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
			case SYSTEM:
				if(saveToken&1){
					err.errCode=addAction(prog,a,debug);
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
		case READ_BREAKPOINT:
		case READ_LABEL:
		case READ_UNDEF:
			if((t.len<1)||a.type!=INVALID){
				err.errCode=ERR_INVALID_IDENTIFER;
				goto errorCleanup;
			}
			if(prevState==READ_MACRO_ACTION){
				err.errCode=ERR_UNFINISHED_MACRO;
				goto errorCleanup;//unfinished macro
			}
			name=copyString(t);
			if(name.len>0&&name.chars==NULL){
				err.errCode=ERR_MEM;
				goto errorCleanup;
			}
			switch(state){
			case READ_BREAKPOINT:{
				Action a=(Action){.type=BREAKPOINT,
					.data.asString=name,
					.at=makePosPtr(err.pos)};
				if(prevState==READ_ACTION){
					err.errCode = addAction(prog,a,debug);
				}else{
					err.errCode = addAction(&tmpMacro,a,debug);
				}
				if(err.errCode){
					goto errorCleanup;
				}
				name.chars=NULL;//unlink to prevent double free
				name.len=0;
			}break;
			case READ_LABEL:{
				err.errCode=putLabel(prog,err.pos,macroMap,name);
				if(err.errCode){
					goto  errorCleanup;
				}
				name.chars=NULL;//unlink to prevent double free
				name.len=0;
			}break;
			case READ_UNDEF:{
				Mapable prevVal=mapPut(macroMap,name,
					(Mapable){.type=MAPABLE_NONE,.value.asPos=0});
				if(prevVal.type==MAPABLE_NONE&&prevVal.value.asPos!=0){
					err.errCode=prevVal.value.asPos;
					goto errorCleanup;
				}
				name.chars=NULL;//unlink to prevent double free
				name.len=0;
			}break;
			default:
				assert(false&&"unreachable");
			}
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
					ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth,debug);
					if(tmp.errCode){
						err=tmp;
						goto errorCleanup;
					}
				}else{
					err.errCode=ERR_UNFINISHED_IDENTIFER;
					goto errorCleanup;
				}
			}else{
				ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth,debug);
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
				ErrorInfo tmp=include(prog,err.pos,macroMap,t,filePath,depth,debug);
				if(tmp.errCode){
					goto errorCleanup;
				}
			}else{
				err.errCode=ERR_UNFINISHED_IDENTIFER;
				goto errorCleanup;
			}
			assert(0&&"unimplemented");
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


char INT_IO_BUFFER [sizeof(uint64_t)];
//count is assumed to be between 1 and 8 (inclusive)
//reads 1-8 bytes from memory and stores the in the lower bytes of the return value
//is an error occurs err is set to a nonzero value otherwise err is set to 0
static uint64_t memReadInternal(ProgState* state,uint64_t addr,uint8_t count,ErrorCode* err){
	if(addr>MAX_MEM_ADDR){
		*err=ERR_HEAP_ILLEGAL_ACCESS;
		return 0;
	}else if(addr>state->stackStart){
		*err=heapReadReversed(state->stack,MAX_MEM_ADDR-addr,INT_IO_BUFFER,count);
	}else{
		*err=heapRead(state->heap,addr,INT_IO_BUFFER,count);
		if(*err){
			return 0;
		}
	}
	uint64_t ret=0;
	for(int i=0;i<count;i++){
		ret|=((uint64_t)(INT_IO_BUFFER[i]&0xffULL))<<8*i;
	}
	return ret;
}
//reads a value from memory
//is an error occurs err is set to a nonzero value otherwise err is set to 0
uint64_t memRead(ProgState* state,ErrorCode* err){
	return memReadInternal(state,state->regA,8,err);
}

//writes a value to memory
//is an error occurs the return value is a nonzero value otherwise 0 is returned
ErrorCode memWrite(ProgState* state){
	for(int i=0;i<sizeof(uint64_t);i++){
		INT_IO_BUFFER[i]=(state->regA>>8*i)&0xff;
	}
	if(state->regB>MAX_MEM_ADDR){
		return ERR_HEAP_ILLEGAL_ACCESS;
	}else if(state->regB>state->stackStart){
		return heapWriteReversed(state->stack,MAX_MEM_ADDR-state->regB,
				INT_IO_BUFFER,sizeof(uint64_t));
	}else{
		return heapWrite(state->heap,state->regB,INT_IO_BUFFER,sizeof(uint64_t));
	}
}

//TODO? compile program to C?

ErrorInfo runProgram(Program prog,ProgState* state,DebugInfo* debug){
	ErrorCode errCode=NO_ERR;
	uint64_t tmp;
	for(;state->ip<prog.len;){
		state->jumped=false;
		switch(prog.actions[state->ip].type){
			case LABEL:
				errCode=ERR_UNRESOLVED_LABEL;
				break;//unresolved label
			case INVALID:
				case INCLUDE:
			case BREAKPOINT:
				//depending on first char of name enabled/disabled by default
				//normally enabled, starting with '!' inverted
				if(debug){
					bool flip=prog.actions[state->ip].data.asString.chars[0]=='!';
					if(flip^(mapGet(debug->breakFlips,
						prog.actions[state->ip].data.asString).type==MAPABLE_NONE)){
						errCode=ERR_BREAKPOINT;
						break;
					}
				}
				break;
			case COMMENT_START:
			case LABEL_DEF:
			case UNDEF:
			case IFDEF:
			case IFNDEF:
			case ELSE:
			case ENDIF:
			case MACRO_START:
			case MACRO_END:
				errCode=ERR_UNRESOLVED_MACRO;
				break;//unresolved macro/label (un)definition
			case FLIP://flip lowest bit
				state->regA^=1;
				break;
			case ROT:;//bit rotation
				int count=0x3f&(prog.actions[state->ip].data.asInt);
				state->regA=(state->regA>>count)|(state->regA<<(64-count));
				break;
			case SWAP:
				tmp=state->regA;
				state->regA=state->regB;
				state->regB=tmp;
				break;
			case LOAD_INT:
				state->regA=prog.actions[state->ip].data.asInt;
				break;
			case LOAD:
				state->regA=memRead(state,&errCode);
				break;
			case STORE:
				errCode=memWrite(state);
				break;
			case JUMPIF:
				if(state->regA&1){
					tmp=state->ip+1;
					state->ip=state->regB;
					state->regB=tmp;
					state->jumped=true;
				}
				break;
			case SYSTEM:;
				int regID=state->regB&SYS_REG_MASK;
				if(regID==0){
					switch(state->regA){
					case CALL_RESIZE_HEAP:
						state->regA=heapEnsureCap(&state->heap,state->sysReg[1],state->stackStart);
						break;
					case CALL_RESIZE_STACK:
						state->regA=heapEnsureCap(&state->stack,state->sysReg[1],MAX_MEM_ADDR+1-heapSize(state->heap));
						state->stackStart=MAX_MEM_ADDR-heapSize(state->stack)+1;
						break;
					case CALL_READ:{
						char* buffer=malloc(state->REG_IO_COUNT);
						if(!buffer){
							errCode=ERR_MEM;
							break;
						}
						state->regA=readWrapper(state->REG_IO_FD,buffer,
								state->PTR_REG_IO_COUNT);
						state->sysReg[0]|=REG_COUNT_MASK;
						if(state->REG_IO_TARGET>MAX_MEM_ADDR){
							free(buffer);
							errCode=ERR_HEAP_ILLEGAL_ACCESS;
							break;
						}
						if(state->REG_IO_TARGET>=state->stackStart){
							errCode=heapWriteReversed(state->stack,MAX_MEM_ADDR-state->REG_IO_TARGET,
									buffer,state->REG_IO_COUNT);
						}else if(state->REG_IO_TARGET+state->REG_IO_COUNT>=state->stackStart){
							uint64_t off=state->stackStart-state->REG_IO_TARGET;
							errCode=heapWriteReversed(state->stack,state->stackStart,
									buffer+off,state->REG_IO_COUNT-off);
						}//no else
						if(errCode==NO_ERR&&state->REG_IO_TARGET<state->stackStart){
							uint64_t count=state->stackStart-state->REG_IO_TARGET;
							count=count>state->REG_IO_COUNT?state->REG_IO_COUNT:count;
							errCode=heapWrite(state->heap,state->REG_IO_TARGET,buffer,count);
						}
						if(errCode){
							free(buffer);
							break;
						}
						free(buffer);
					}break;
					case CALL_WRITE:{
						char* buffer=malloc(state->REG_IO_COUNT);
						if(!buffer){
							errCode=ERR_MEM;
							break;
						}
						if(state->REG_IO_TARGET>MAX_MEM_ADDR){
							free(buffer);
							errCode=ERR_HEAP_ILLEGAL_ACCESS;
							break;
						}
						if(state->REG_IO_TARGET>=state->stackStart){
							errCode=heapReadReversed(state->stack,MAX_MEM_ADDR-state->REG_IO_TARGET,
									buffer,state->REG_IO_COUNT);
						}else if(state->REG_IO_TARGET+state->REG_IO_COUNT>=state->stackStart){
							uint64_t off=state->stackStart-state->REG_IO_TARGET;
							errCode=heapReadReversed(state->stack,state->stackStart,
									buffer+off,state->REG_IO_COUNT-off);
						}//no else
						if(errCode==NO_ERR&&state->REG_IO_TARGET<state->stackStart){
							uint64_t count=state->stackStart-state->REG_IO_TARGET;
							count=count>state->REG_IO_COUNT?state->REG_IO_COUNT:count;
							errCode=heapRead(state->heap,state->REG_IO_TARGET,buffer,count);
						}
						if(errCode){
							free(buffer);
							break;
						}
						state->regA=writeWrapper(state->REG_IO_FD,buffer,
								state->PTR_REG_IO_COUNT);
						state->sysReg[0]|=REG_COUNT_MASK;
						free(buffer);
					}break;
					default:
						state->regA=-1;//TODO errorCodes
						break;
					}
				}else{
					tmp=state->sysReg[regID];
					state->sysReg[regID]=state->regA;
					//read value only if output available
					if(state->sysReg[0]&(1<<regID)){
						state->sysReg[0]^=(1<<regID);//clear readable flag
						state->regA=tmp;
					}
				}
				break;
		}
		if(errCode){
			return (ErrorInfo){
				.errCode=errCode,
				.pos=prog.actions[state->ip].at?*prog.actions[state->ip].at:
						NULL_POS,
			};
		}
		if(debug){
			debug->maxSteps--;
			if(debug->maxSteps==0){
				return (ErrorInfo){
					.errCode=ERR_BREAK_STEP,
					.pos=prog.actions[state->ip].at?*prog.actions[state->ip].at:
							NULL_POS,
				};
			}
		}//no else
		if(!state->jumped){
			state->ip++;
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
	case ERR_BREAKPOINT:
		fputs("Breakpoint\n",stderr);
		break;
	case ERR_BREAK_STEP:
		fputs("reached max step count\n",stderr);
		break;
	case ERR_BREAK_AT:
		fputs("reached break condition\n",stderr);
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


static void printDebugInfo(){
	puts("\ncommands for debug mode:");
	puts("step: executes one instruction");
	puts("step <int>: executes the given amount of instructions");
	puts("enable  <breakPointId>: enables the breakpoints with the given id");
	puts("disable <breakPointId>: disables the breakpoints with the given id");
	puts("sys_regs: displays the sys-registers");
	puts("mem <off> <len>: disables <len> 64bit-memblocks starting at <off>");
	puts("mem <label> <off> <len>: disables <len> 64bit-memblocks starting at <off> "
			"labeled with the given label after every change of the program");
	puts("unreg <label>: removes a labeled section of memory");
	// breakAt <command>
}
/**
 * reads a line from the console and parses the debug-commands,
 * returns the char-code of the first line-separator that got detected
 * */
static int readDebugCommands(DebugInfo* info,char prevLineSep){
	fputs("> ",stdout);
	fflush(stdout);
	char* buffer=malloc(READ_DEBUG_INIT_CAP);
	size_t i=0,buffer_cap=READ_DEBUG_INIT_CAP;
	int c,r=0;
	String str;
	DebugReadState state=READ_COMMAND;
	bool readInput=false;
	info->maxSteps=0;
	String memName;
	uint64_t memOff;
	MemDisplayMode memMode;
	MemDisplay** itr;
	bool isU64;
	while((c=getc(stdin))!=EOF){
		if(isspace(c)){
			if(r==0){
			str.chars=buffer;
			str.len=i;
			if(i>0){
				switch(state){
				case READ_STEP_COUNT:;
					uint64_t ll=u64fromStr(str,&isU64);
					state=READ_COMMAND;
					if(isU64){
						ll+=info->maxSteps;//overflow protection
						info->maxSteps=ll>info->maxSteps?ll:SIZE_MAX;
						break;
					}else{
						if(info->maxSteps<SIZE_MAX)
							info->maxSteps++;
						//fall-though to read command
					}
					//no break
				case READ_COMMAND:
					if(strCaseEq("s",str)||strCaseEq("step",str)){//step
						state=READ_STEP_COUNT;
					}else if(strCaseEq("enable",str)){//enable
						state=READ_ENABLE_ID;
					}else if(strCaseEq("disable",str)){//disable
						state=READ_DISABLE_ID;
					}else if(strCaseEq("breakAt",str)){//breakAt
						state=READ_BREAK_AT_ID;
					}else if(strCaseEq("sys_regs",str)){//sys_regs
						info->showSysRegs=!info->showSysRegs;
					}else if(strCaseEq("run",str)||strCaseEq("r",str)){//run
						info->maxSteps=SIZE_MAX;
					}else if(strCaseEq("quit",str)||strCaseEq("q",str)
							||strCaseEq("exit",str)||strCaseEq("x",str)){//quit
						exit(0);//XXX? other action for quit
					}else if(strCaseEq("?",str)||strCaseEq("help",str)){//help
						printDebugInfo();
					}else if(strCaseEq("mem",str)){//mem
						memMode=DISPLAY_INT64;//XXX commands for other mem-modes
						state=READ_MEM_NAME;
					}else if(strCaseEq("unregister",str)||strCaseEq("unreg",str)){//unregister
						state=READ_UNREG_NAME;
					}else{
						printf("unknown debug command: %.*s\n",(int)str.len,str.chars);
						r=-1;
						break;
					}
					break;
				case READ_DISABLE_ID:
				case READ_ENABLE_ID:
					if((str.chars[0]=='!')==(state==READ_ENABLE_ID)){
						//add to breakFlips
						String clone=copyString(str);
						if(clone.len>0&&clone.chars==NULL){
							puts("memory error while parsing");
							r=-1;
							break;
						}
						Mapable ret=mapPut(info->breakFlips,clone,
								(Mapable){.type=MAPABLE_POS,.value.asPos=1});
						if(ret.type==MAPABLE_NONE&&ret.value.asPos!=0){
							puts("memory error while parsing");
							r=-1;
							break;
						}
					}else{//remove from breakFlips
						Mapable ret=mapPut(info->breakFlips,str,
								(Mapable){.type=MAPABLE_NONE,.value.asPos=0});
						if(ret.type==MAPABLE_NONE&&ret.value.asPos!=0){
							puts("memory error while parsing");
							r=-1;
							break;
						}
					}
					state=READ_COMMAND;
					break;
				case READ_BREAK_AT_ID:
					//TODO breakAt ID
					assert(false&&"unimplemented");
					state=READ_COMMAND;
					break;
				case READ_MEM_NAME:
					memOff=u64fromStr(str,&isU64);
					if(isU64){
						memName=(String){.len=0,.chars=NULL};
						state=READ_MEM_LEN;
					}else{
						memName=copyString(str);
						if(!memName.chars){
							puts("memory error while parsing");
							r=-1;
						}
						state=READ_MEM_OFF;
					}
					break;
				case READ_MEM_OFF:
					memOff=u64fromStr(str,&isU64);
					if(isU64){
						state=READ_MEM_LEN;
					}else{
						printf("illegal input for mem-offset: %.*s\n",
								(int)str.len,str.chars);
						r=-1;
					}
				break;
				case READ_MEM_LEN:;
					MemDisplay* mem=malloc(sizeof(MemDisplay));
					if(!mem){
						puts("memory error while parsing");
						r=-1;
						break;
					}
					mem->label=memName;
					mem->first=true;
					mem->mode=memMode;
					mem->addr=memOff;
					mem->next=info->memDisplays;
					bool isU64;
					mem->count=u64fromStr(str,&isU64);
					if(isU64){
						info->memDisplays=mem;
						state=READ_COMMAND;
					}else{
						printf("illegal input for mem-size: %.*s\n",
								(int)str.len,str.chars);
						r=-1;
					}
					break;
				case READ_UNREG_NAME:
					itr=&info->memDisplays;
					while(*itr){
						if((*itr)->label.len==str.len&&
							strncmp((*itr)->label.chars,str.chars,str.len)==0){
							//unlink display
							free((*itr)->label.chars);
							MemDisplay* tmp=*itr;
							*itr=(*itr)->next;
							free(tmp);
						}else{
							itr=&((*itr)->next);
						}
					}
					state=READ_COMMAND;
					break;
				}
			}
			i=0;
			}
			if(c=='\r'){
				r=r==0?c:r;
				break;
			}else if(c=='\n'){
				r=r==0?c:r;
				if(readInput&&prevLineSep!='\r'){
					break;//continue reading if \r is directly followed by \n
				}
			}
		}else if(r>=0){
			if(i>=buffer_cap){
				char* tmp=realloc(buffer,2*buffer_cap);
				if(!tmp){
					puts("memory error while parsing");
					r=-1;
					continue;//continue reading until end of line
				}
				buffer=tmp;
				buffer_cap*=2;
			}
			buffer[i++]=c;
		}
		readInput=true;
	}
	if(r<0){
		r=0;
	}else{
		switch(state){
		case READ_COMMAND:
			break;
		case READ_STEP_COUNT:
			if(info->maxSteps<SIZE_MAX)
				info->maxSteps++;
			break;
		case READ_ENABLE_ID:
		case READ_DISABLE_ID:
		case READ_BREAK_AT_ID:
		case READ_MEM_NAME:
		case READ_MEM_OFF:
		case READ_MEM_LEN:
		case READ_UNREG_NAME:
			r=0;
			puts("missing arguments");
			break;
		}
	}
	free(buffer);
	return r;
}

static void printMemInfo(ProgState *state, _Bool changed,DebugInfo *debug) {
	MemDisplay **itr;
	uint64_t tmp;
	ErrorCode ioCode;
	itr = &debug->memDisplays;
	while ((*itr)) {
		if (changed || (*itr)->first) {
			if ((*itr)->label.len > 0) {
				printf("%.*s (%"PRIx64"): ",
						(int) (*itr)->label.len,
						(*itr)->label.chars,
						(*itr)->addr);
				(*itr)->first = false;
			} else {
				printf("%"PRIx64": ", (*itr)->addr);
			}
			int blockSize = 0;
			switch ((*itr)->mode) {
			case DISPLAY_CHAR:
				blockSize = 1;
				break;
			case DISPLAY_BYTE:
				blockSize = 1;
				break;
			case DISPLAY_INT16:
				blockSize = 2;
				break;
			case DISPLAY_INT32:
				blockSize = 4;
				break;
			case DISPLAY_INT64:
				blockSize = 8;
				break;
			}
			for (size_t i = 0; i < (*itr)->count; i++) {
				tmp = memReadInternal(state,
						(*itr)->addr + i * blockSize,
						blockSize, &ioCode);
				if (ioCode) {
					fputs("? ", stdout);
				} else {
					switch ((*itr)->mode) {
					case DISPLAY_CHAR:
						printf("'%c' ",
								(char) (tmp & 0xff));
						break;
					case DISPLAY_BYTE:
						printf("%.2x ",
								(uint8_t) (tmp & 0xff));
						break;
					case DISPLAY_INT16:
						printf("%.4x ",
								(uint16_t) (tmp & 0xffff));
						break;
					case DISPLAY_INT32:
						printf("%.8x ",
								(uint32_t) (tmp
										& 0xffffffff));
						break;
					case DISPLAY_INT64:
						printf("%.16"PRIX64" ", tmp);
						break;
					}
				}
			}
			puts(""); //new line
			if ((*itr)->label.len == 0) {
				//remove unlabeled mem-sections after display
				MemDisplay *delSection = *itr;
				*itr = (*itr)->next;
				free(delSection);
			} else {
				itr = &((*itr)->next);
			}
		} else {
			itr = &((*itr)->next);
		}
	}
}

ErrorInfo debugProgram(Program prog,ProgState* state){
	ErrorInfo res={
		.errCode=NO_ERR,
		.pos.file=(String){.len=0,.chars=NULL}
	};
	DebugInfo debug={
		.maxSteps=SIZE_MAX,
		.breakFlips=createHashMap(1024),
		.showSysRegs=false,
		.memDisplays=NULL,
	};
	if(!debug.breakFlips){
		return (ErrorInfo){
			.errCode=ERR_UNRESOLVED_LABEL,
			.pos=prog.actions[state->ip].at?*prog.actions[state->ip].at:
					NULL_POS,
		};
	}
	puts("Debugging program:");
	int r=0;
	bool isBreak=true,changed;
	String bp_name;
	do{
		do{
			r=readDebugCommands(&debug,r);
		}while(r==0);
		changed=false;
		if(debug.maxSteps>0){
			changed=true;
			isBreak=false;
			res=runProgram(prog,state,&debug);
			if(res.errCode==ERR_BREAKPOINT){
				isBreak=true;
				bp_name=prog.actions[state->ip].data.asString;
				printf("\n break_point \"%.*s\"",(int)bp_name.len,bp_name.chars);
			}else if(res.errCode==ERR_BREAK_STEP){
				isBreak=true;
				printf("\n reached step count");
			}else if(res.errCode==ERR_BREAK_AT){
				isBreak=true;
				assert(false&&"unimplemented");//TODO handle conditional break
				printf("\n fulfilled break condition");
			}
			if(isBreak){
				printf(" in line:%"PRIu64", pos:%"PRIu64" in %.*s\n"
						" regA: %"PRIx64" regB: %"PRIx64" ip: %"PRIx64"\n",
						(uint64_t)res.pos.line,(uint64_t)res.pos.posInLine,
						(int)res.pos.file.len,res.pos.file.chars,
						state->regA,state->regB,state->ip);
				if(!state->jumped){
					state->ip++;//increase ip after break
				}
			}//no else
			if(debug.showSysRegs){
				fputs(" sys registers: ",stdout);
				for(int i=1;i<SYS_REG_COUNT;i++){
					if(state->sysReg[0]&1<<i){
						printf("%"PRIx64" ",state->sysReg[i]);
					}else{
						printf("(%"PRIx64") ",state->sysReg[i]);
					}
				}
				puts("");
			}
		}
		printMemInfo(state,changed, &debug);
	}while(isBreak);
	puts("\nExecution finished:");
	printMemInfo(state,true, &debug);
	return res;
}

void printUsage(){
	puts("usage:");
	puts("<filename>");
	puts("-d <filename>");
	puts("<filename> -lib <libDir> ");
	puts("");
}


//TODO update stack.frs to use dynamic stack-size
int main(int argc,char** argv) {
	if(argc<2){
		puts("no file name provided");
		printUsage();
		return EXIT_FAILURE;
	}
	FILE* file=NULL;
	char* path;
	bool debug=false;
	if(argc==2){
		path=argv[1];
		file=fopen(argv[1],"r");;
		if(!file){
			fprintf(stderr,"File %s not found",path);
			return EXIT_FAILURE;
		}
		libPath=getDirFromPath((String){.chars=argv[0],.len=strlen(argv[0])});
		//append DEF_LIB_DIR_NAME to libPath
		char* tmp=malloc(libPath.len+strlen(DEF_LIB_DIR_NAME));
		if(!tmp){
			puts("out of memory");
			return EXIT_FAILURE;
		}
		memcpy(tmp,libPath.chars,libPath.len);
		memcpy(tmp+libPath.len,DEF_LIB_DIR_NAME,strlen(DEF_LIB_DIR_NAME));
		libPath.chars=tmp;
		libPath.len+=strlen(DEF_LIB_DIR_NAME);
	}else{
		bool needsLib=true;
		for(int i=1;i<argc;i++){
			if(strcmp(argv[i],"-lib")==0){
				i++;//read next argument
				if(i>=argc){
					puts("missing argument for lib");
					printUsage();
					return EXIT_FAILURE;
				}
				libPath=(String){.chars=argv[i],.len=strlen(argv[i])};
				//ensure libPath ends with file separator
				char* tmp=malloc(libPath.len+1);
				if(!tmp){
					puts("out of memory");
					return EXIT_FAILURE;
				}
				memcpy(tmp,libPath.chars,libPath.len);
				libPath.chars=tmp;
				if((tmp[libPath.len-1]!='/'&&tmp[libPath.len-1]!='\\')){
					tmp[libPath.len++]='/';
				}
				needsLib=false;
			}else if(strcmp(argv[i],"-d")==0){
				debug=true;
			}else {
				if(file!=NULL){
					puts("multiple files specified");
					printUsage();
					return EXIT_FAILURE;
				}
				file=fopen(argv[i],"r");
				path=argv[i];
				if(!file){
					fprintf(stderr,"File %s not found",path);
					return EXIT_FAILURE;
				}
			}
		}
		if(file==NULL){
			puts("no file specified");
			printUsage();
			return EXIT_FAILURE;
		}
		if(needsLib){
			libPath=getDirFromPath((String){.chars=argv[0],.len=strlen(argv[0])});
		}
	}
	String filePath={.chars=path,.len=strlen(path)};
	//for debug purposes
	printf("root: %.*s\n",(int)libPath.len,libPath.chars);
	Program prog;
	reinitMacro(&prog);
	HashMap* macroMap=createHashMap(MAP_CAP);
	ErrorInfo res=readFile(file,&prog,macroMap,filePath,filePath,0,debug);
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
			.ip=0,
			.regA=0,
			.regB=0,
			.stack = createHeap(),
			.stackStart=MAX_MEM_ADDR+1,
			.heap  = createHeap(),
			.sysReg = {0},
	};
	if(!(initState.stack.sections&&initState.heap.sections)){
		fputs("Out of memory",stderr);
		return EXIT_FAILURE;
	}
	fflush(stdout);
	if(debug){
		res=debugProgram(prog,&initState);
	}else{
		res=runProgram(prog,&initState,NULL);
	}
	if(res.errCode){
		fprintf(stderr,"Error while executing Program\n");
		printError(res);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
