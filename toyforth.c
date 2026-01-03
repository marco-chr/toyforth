#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

#define TFOBJ_TYPE_INT 0
#define TFOBJ_TYPE_STR 1
#define TFOBJ_TYPE_BOOL 2
#define TFOBJ_TYPE_LIST 3
#define TFOBJ_TYPE_SYMBOL 4

/* ========================= Data Structures ==============================*/

typedef struct tfobj {
	int refcount;
	int type;
	union {
		int i;
		struct {
			char *ptr;
			size_t len;
		} str;
		struct {
			struct tfobj **ele;
			size_t len;
		} list;
	};
} tfobj;

typedef struct tfparser {
	char *prg; // program to compile into a list.
	char *p; // Next token to parse.
} tfparser;

/* Function table entry: each of this entry represents a symbol name
 * associated with a function implementation */
struct tfctx;

typedef struct FunctionTableEntry{
	tfobj *name;
	void (*callback) (struct tfctx *ctx, tfobj *name);
	tfobj *user_func;
} tffuncentry;

struct FunctionTable{
	tffuncentry **func_table;
	size_t func_count;
};

/* Execution context */
typedef struct tfctx {
	tfobj *stack;
	struct FunctionTable functable;
} tfctx;

/* Function Prototypes */

void retain(tfobj *o);
void release(tfobj *o);

/* ============================== Wrappers ==============================*/

void *xmalloc(size_t size) {
	void *ptr = malloc(size);
	if (ptr == NULL) {
		fprintf(stderr,"Out of memory allocating %zu bytes\n", size);
		exit(1);
	}
	return ptr;
}

void *xrealloc(void *oldptr, size_t size){
	void *ptr = realloc(oldptr, size);
	if (ptr == NULL) {
		fprintf(stderr,"Out of memory allocating %zu bytes\n", size);
		exit(1);
	}
	return ptr;
}

/* ===================== Object related functions =======================*/

tfobj *createObject(int type) {
	tfobj *o = xmalloc(sizeof(tfobj));
	o->type = type;
	o->refcount = 1;
	return o;
}

tfobj *createIntObject(int i){
	tfobj *o = createObject(TFOBJ_TYPE_INT);
	o->i = i;
	return o;
}

tfobj *createBoolObject(int i){
	tfobj *o = createObject(TFOBJ_TYPE_BOOL);
	o->i = i;
	return o;
}

/* Free an object and all other nested objects */
void freeObject(tfobj *o){
	switch(o->type){
	case TFOBJ_TYPE_LIST:
		for(size_t j = 0; j < o->list.len; j++){
			tfobj *ele = o->list.ele[j];
			release(ele);
		}
		break;
	case TFOBJ_TYPE_SYMBOL:
	case TFOBJ_TYPE_STR:
		free(o->str.ptr);
		break;
	}
	free(o);
}

void retain(tfobj *o){
	o->refcount++;
}

void release(tfobj *o){
	assert(o->refcount > 0);
	o->refcount--;
	if (o->refcount == 0) freeObject(o);
}

void printObject(tfobj *o){
	switch(o->type){
	case TFOBJ_TYPE_INT:
		printf("%d", o->i);
		break;
	case TFOBJ_TYPE_LIST:
		printf("[");
		for(size_t j = 0; j < o->list.len; j++){
			tfobj *ele = o->list.ele[j];
			printObject(ele);
			if (j != o->list.len-1)
			printf(" ");
		}
		printf("]");
		break;
	case TFOBJ_TYPE_STR:
		printf("\"%s\"", o->str.ptr);
		break;
	case TFOBJ_TYPE_SYMBOL:
		printf("%s", o->str.ptr);
		break;
	default:
		printf("?");
		break;
	}
}

/* ======================== String Object ===========================*/

tfobj *createStringObject(char *s, size_t len){
	tfobj *o = createObject(TFOBJ_TYPE_STR);
	o->str.ptr = xmalloc(len+1);
	o->str.len = len;
	memcpy(o->str.ptr, s, len);
	o->str.ptr[len] = 0;
	return o;
}

tfobj *createSymbolObject(char *s, size_t len){
	tfobj *o = createStringObject(s,len);
	o->type = TFOBJ_TYPE_SYMBOL;
	return o;
}

int compareStringObject(tfobj *a, tfobj *b) {
	size_t minlen = a->str.len < b->str.len ? a->str.len : b->str.len;
	int cmp = memcmp(a->str.ptr, b->str.ptr, minlen);

	if (cmp == 0) {
		if (a->str.len == b->str.len) return 0;
		else if (a->str.len > b->str.len) return 1;
		else return -1;
	} else {
		if (cmp < 0) return -1;
		else return 1;
	}
}

/* ========================== List Object ===========================*/

tfobj *createListObject(void){
	tfobj *o = createObject(TFOBJ_TYPE_LIST);
	o->list.ele = NULL;
	o->list.len = 0;
	return o;
}

/* Add the new element at the end of the list 'list'. */
/* It is up to the caller to increment the ref count */

 void listPush(tfobj *l, tfobj *ele) {
	l->list.ele = xrealloc(l->list.ele, sizeof(tfobj*) * (l->list.len+1));
	l->list.ele[l->list.len] = ele;
	l->list.len++;
	}

/* =============== Turn program into Toy Forth list =================*/

void parseSpaces(tfparser *parser){
	while(isspace(parser->p[0]))parser->p++;
	}

#define MAX_NUM_LEN 128

tfobj *parseNumber(tfparser *parser){
	char buf[MAX_NUM_LEN];
	char *start = parser->p;
	char *end;

	if (parser->p[0] == '-') parser->p++;
	while(parser->p[0] && isdigit(parser->p[0])) parser->p++;
	end = parser->p;
	int numlen = end - start;
        
	if (numlen >= MAX_NUM_LEN) return NULL;

	memcpy(buf,start,numlen);
	buf[numlen]=0;

	tfobj *o = createIntObject(atoi(buf));
	return o;

	
}

int is_symbol_char(int c){
	char symchars[] = "+-*/%[]{}";
	return isalpha(c) || strchr(symchars,c) != NULL;
}

tfobj *parseSymbol(tfparser *parser){
	char *start = parser->p;
	while(parser->p[0] && is_symbol_char(parser->p[0])) parser->p++;
	int numlen = parser->p - start;
	return createSymbolObject(start,numlen);
}

tfobj *compile(char *prg){
	tfparser parser;
	parser.prg = prg;
	parser.p = prg;

	tfobj *parsed = createListObject();

	while(parser.p) {
		tfobj *o;
		char *token_start = parser.p;

		parseSpaces(&parser);
		if (parser.p[0] == 0) break;

		if (isdigit(parser.p[0]) || 
		(parser.p[0] == '-' && isdigit(parser.p[1]))) 
		{
			o = parseNumber(&parser);
		} else if(is_symbol_char(parser.p[0])) {
			o = parseSymbol(&parser);
		} else {
		 	o = NULL;
		}

		// Check if current token produced a parsing error
		if (o == NULL) {
			release(parsed);
			printf("Syntax error near: %32s\n", token_start);
			return NULL;
		} else {
			listPush(parsed,o);
		}
	}
	return parsed;
}
/* ====================== Basic Math Functions =========================*/
void basicMathFunctions(tfctx *ctx, tfobj *name){
	if (ctxCheckStackMinLen(ctx,2)) return;
	tfobj *b = ctxStackPop(ctx, TFOBJ_TYPE_INT);
	tfobj *a = ctxStackPop(ctx, TFOBJ_TYPE_INT);
	if (a == NULL || b == NULL) return;

	int result;
	switch(name->str.ptr[0]) {
		case '+': result = a->i + b->i; break;
	}	  
	ctxStackPush(ctx, createIntObject(result));
}


/* ====================== Execution and Context =========================*/

tffuncentry *getFunctionByName(tfctx *ctx, tfobj *name) {
	for (size_t j = 0; j < ctx->functable.func_count; j++){
		tffuncentry *fe = ctx->functable.func_table[j];
		if (compareStringObject(fe->name, name) == 0)
			return fe;
	}
	return NULL;
}

/* Push a new function entry in the context. It is up to the caller
 * to set either C callback or the list representing the user defined
 * function. */
tffuncentry *registerFunction(tfctx *ctx, tfobj *name){
	ctx->functable.func_table = xrealloc(ctx->functable.func_table, 
			sizeof(tffuncentry*) * (ctx->functable.func_count+1));
	tffuncentry *fe = xmalloc(sizeof(tffuncentry));
	ctx->functable.func_table[ctx->functable.func_count] = fe;
	ctx->functable.func_count++;
	fe->name = name;
	retain(name);
	fe->callback = NULL;
	fe->user_func = NULL;
	return fe;
}

/* Register a new function with given name in the function table 
 * of the context. The function can't fail since if a function with 
 * same name already exist, gets replaced */

void registerCFunction(tfctx *ctx, char *name,
	       	void (*callback) (tfctx *ctx, tfobj *name))
{
	tffuncentry *fe;
	tfobj *oname = createStringObject(name, strlen(name));
	fe = getFunctionByName(ctx,oname);
	if (fe) {
	   if (fe->user_func) {
		   release(fe->user_func);
		   fe->user_func = NULL;
	   }
	   fe->callback = callback;
	} else {
	   fe = registerFunction(ctx,oname);
	   fe->callback = callback;
	}
	release(oname);
}

tfctx *createContext(void) {
	tfctx *ctx = xmalloc(sizeof(*ctx));
	ctx->stack = createListObject();
	ctx->functable.func_table = NULL;
	ctx->functable.func_count = 0;
	registerCFunction(ctx,"+",basicMathFunctions);
	return ctx;
}	

/* Try to resolve and call the function associated with the symbol
 * name 'word'. Return 0 if the symbol is bound to a function, return 1
 * otherwise */
int callSymbol(tfctx *ctx, tfobj *word){
	tffuncentry *fe = getFunctionByName(ctx, word);
	if (fe == NULL) return 1;
	return 0;	
}

void exec(tfctx *ctx, tfobj *prg) {
	assert(prg->type == TFOBJ_TYPE_LIST);
	for(size_t j = 0; j < prg->list.len; j++){
		tfobj *word = prg->list.ele[j];
		switch(word->type) {
			case TFOBJ_TYPE_SYMBOL:
				callSymbol(ctx, word);
				break;
			default:
				listPush(ctx->stack, word);
				retain(word);
				break;
		}
	}
}

/* ============================== Main ==============================*/
int main (int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr,"Usage: %s <filename>\n", argv[0]);
		return 1;
	}

	/* Read the program in memory, for later parsing */
	FILE *fp = fopen(argv[1],"r");
	if (fp == NULL) {
		perror("Opening Toy Forth program");
		return 1;
	}
	
	// compute file size
	fseek(fp,0,SEEK_END);
	long file_size = ftell(fp);
	char *prgtext = xmalloc(file_size+1);
	// place fp at beginning of file
	fseek(fp,0,SEEK_SET);
	fread(prgtext,file_size,1,fp);
	prgtext[file_size] = 0;
	fclose(fp);

	// printf("Program text: %s\n",prgtext);
	tfobj *prg = compile(prgtext);
	printObject(prg);
	printf("\n");

	tfctx *ctx = createContext();
	exec(ctx,prg);

	// print stack content
	printf("Stack content at end: ");
	printObject(ctx->stack);
	printf("\n");
	return 0;	
}



