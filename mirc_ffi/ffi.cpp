#include "ffi.h"

map<string, ffiObject*> objects;
map<string, ffiStruct*> structs;
ffiCall *lastCall = NULL;

// ffiObject

ffiObject::ffiObject(string name) : imageName(name) 
{
	imageHandle = LoadLibraryA(name.c_str());
}

ffiObject::~ffiObject()
{
	map<string, ffiFunction*>::iterator it = functions.begin();
	for (it; it != functions.end(); it++) delete it->second;
	FreeLibrary(imageHandle);
}

// ffiFunction

ffiFunction::ffiFunction(ffiObject& object, string name) : owner(object), functionName(name), returnType(NULL)
{
	funcPtr = (VARIFUNC*)GetProcAddress(object.imageHandle, name.c_str());
}

ffiFunction::~ffiFunction()
{
	delete returnType;
	vector<ffiArgument*>::iterator it = arguments.begin();
	for (it; it != arguments.end(); it++) delete *it;
}

ffiFunction& ffiObject::addFunction(string name)
{
	return *(functions[name] = new ffiFunction(*this, name));
}

void ffiFunction::addArgument(ffiArgument *argument)
{
	arguments.push_back(argument);
}

void ffiFunction::addArgument(string type)
{
	addArgument(new ffiArgument(type));
}

void ffiFunction::setReturnType(ffiArgument *val)
{
	returnType = val;
}

void ffiFunction::setReturnType(string val)
{
	setReturnType(new ffiArgument(val));
}

// ffiCall

ffiCall::ffiCall(ffiFunction& func) : function(func), returnValue(NULL), stackOffset(0)
{
	int total = 0;
	vector<ffiArgument*>::iterator it = function.arguments.begin();
	for (it; it != function.arguments.end(); it++) total += (*it)->size();
	stackData = new char[total];
}

ffiCall::~ffiCall()
{
	delete[] stackData;
	vector<char*>::iterator it;
	for (it = storage.begin(); it != storage.end(); it++) delete[] *it;
}

void ffiCall::call(vector<string> args)
{
	vector<string>::iterator it = args.begin();
	vector<ffiArgument*>::iterator ait = function.arguments.begin();
	for (; ait != function.arguments.end(); it++, ait++) {
		convertArgument(*it, *(*ait));
	}

	// TODO support cdecl calling convention
	ull *stackptr, dwords = stackOffset/sizeof(ull);
	__asm { 
		sub esp, [stackOffset] 
		mov stackptr, esp
	}
	for (unsigned int i = 0; i < dwords; i++) {
		stackptr[i] = ((ull*)stackData)[i];
	}
	returnValue = function.funcPtr();
	if (function.returnType == NULL || function.returnType->isVoid()) returnValue = 0;
}

#define SCAN(type, fmt) type d; sscanf(data, fmt, &d); memcpy(stackData + stackOffset, &d, sizeof(type)); 
#define SCAN_NUMERIC(type) SCAN(type, "%d")
#define SCAN_DECIMAL(type) SCAN(type, "%f")
void ffiCall::convertArgument(string& sData, ffiArgument& argument)
{
	const char *data = sData.c_str();
	char *ptrStorage = NULL;
	istringstream in(data, istringstream::in);
	if (argument.isInteger()) { 
		if (argument.isSigned()) {
			SCAN_NUMERIC(int);
		}
		else { 
			SCAN_NUMERIC(unsigned int); 
		}
	}
	else if (argument.isDecimal()) { 
		if (argument.size() == sizeof(double)) {
			SCAN_DECIMAL(double);
		}
		else {
			SCAN_DECIMAL(float);
		}
	}
	else if (argument.isPointer()) { 
		unsigned int bytes = argument.size(true);
		if (argument.isChar() && sData.length() > bytes) {
			ptrStorage = new char[sData.length() + 1];
		}
		if (ptrStorage == NULL) {
			ptrStorage = new char[bytes];
		}
		if (argument.isChar()) {
			memcpy(ptrStorage, data, sData.length() + 1);
		}
		else {
			memcpy(ptrStorage, stackData+stackOffset, bytes);
		}
		storage.push_back(ptrStorage);
		memcpy(stackData + stackOffset, &ptrStorage, sizeof(char *));
	}
	else if (argument.isChar()) { 
		SCAN(int, "%c");
	}

	if (argument.isCapture()) {
		if (ptrStorage != NULL) {
			captures.push_back(ptrStorage);
		}
	}
	stackOffset += argument.size();
}

#define PRINT(type) long long d = 0; memcpy(&d, data, argument.size()); out << (type)d;
string ffiCall::convertArgumentToString(char *data, ffiArgument& argument)
{
	ostringstream out;
	if (argument.isChar()) {
		return data;
	}
	else if (argument.isInteger()) {
		if (argument.isSigned()) {
			PRINT(long long);
		}
		else {
			PRINT(unsigned long long);
		}
	}
	else if (argument.isDecimal()) {
		PRINT(double);
	}
	return out.str();
}

string ffiCall::getCapture(int index)
{
	int i = -1;
	char *data = captures[index];
	ffiArgument *arg = getCaptureArgument(index);
	if (arg == NULL) return "";
	return convertArgumentToString(data, *arg);
}

ffiArgument *ffiCall::getCaptureArgument(int index)
{
	int i = -1;
	vector<ffiArgument*>::iterator it = function.arguments.begin();
	for (it; it != function.arguments.end(); it++) {
		if ((*it)->isCapture()) i++;
		if (i == index) return *it;
	}
	return NULL;
}

string ffiCall::getCapture(vector<int> index)
{
	int first = index[0];
	int i = 1, offset = 0;
	ffiArgument *arg = getCaptureArgument(first);
	ffiStruct *lastStruct = NULL;
	if (arg == NULL) return "";
	while (arg->isStruct()) {
		lastStruct = structs[arg->typeName];
		if (lastStruct == NULL) return "";
		arg = &lastStruct->getMember(index[i]);
		i++;
	}
	for (int x = 0; x < index[i-1]; x++) {
		offset += lastStruct->getMember(x).size();
	}
	return convertArgumentToString(captures[first] + offset, *arg);
}

string ffiCall::getReturnValue()
{
	return convertArgumentToString((char *)&returnValue, *function.returnType);
}

// ffiArgument

ffiArgument::ffiArgument(string type) : flags(0), bytes(0)
{
	istringstream in(type);
	bool seenName = false;
	char ch = 0;
	while (!in.eof()) {
		ch = 0; in >> ch;
		if (ch == 0) break;
		if (!seenName) {
			if (ch == '*') flags |= FFI_FLAG_POINTER;
			else if (ch == '<') flags |= FFI_FLAG_CAPTURE;
			else if (ch == '@') { flags |= FFI_FLAG_STRUCT; }
			else if (isalpha(ch)) seenName = true;
		}
		
		if (seenName) {
			if (isalpha(ch)) { 
				seenName = true;
				char word[2] = {ch,0};
				typeName.append(word);
			}
			else if (isdigit(ch)) {
				in.unget();
				int val;
				in >> val;
				bytes = val;
			}
		}
	}

	if (!isStruct()) {
		if (typeName == "i") {
			flags |= FFI_FLAG_SIGNED | FFI_FLAG_INTEGER;
			if (bytes == 0) bytes = sizeof(int);
		}
		else if (typeName == "u") {
			flags |= FFI_FLAG_INTEGER;
			if (bytes == 0) bytes = sizeof(unsigned int);
		}
		else if (typeName == "f" || typeName == "float") {
			flags |= FFI_FLAG_DECIMAL;
			bytes = sizeof(float);
		}
		else if (typeName == "d" || typeName == "double") {
			flags |= FFI_FLAG_DECIMAL;
			bytes = sizeof(double);
		}
		else if (typeName == "c" || typeName == "char") {
			flags |= FFI_FLAG_CHAR;
			bytes = 1;
		}
		else if (typeName == "str") {
			flags |= FFI_FLAG_POINTER | FFI_FLAG_CHAR;
		}
		else if (typeName == "void") {
			flags |= FFI_FLAG_VOID;
			bytes = 0;
		}
	}
}

const int ffiArgument::size(bool ignorePointer)
{
	if (!ignorePointer && isPointer()) { return sizeof(void*); }
	else if (isStruct()) {
		map<string,ffiStruct*>::iterator it = structs.find(typeName);
		if (it != structs.end()) return (*it).second->size();
		return 0;
	}
	else {
		return bytes;
	}
}

const BOOL ffiArgument::isSigned() { return flags & FFI_FLAG_SIGNED; }
const BOOL ffiArgument::isInteger() { return flags & FFI_FLAG_INTEGER; }
const BOOL ffiArgument::isDecimal() { return flags & FFI_FLAG_DECIMAL; }
const BOOL ffiArgument::isChar() { return flags & FFI_FLAG_CHAR; }
const BOOL ffiArgument::isStruct() { return flags & FFI_FLAG_STRUCT; }
const BOOL ffiArgument::isPointer() { return flags & FFI_FLAG_POINTER; }
const BOOL ffiArgument::isVoid() { return flags & FFI_FLAG_VOID; }
const BOOL ffiArgument::isCapture() { return flags & FFI_FLAG_CAPTURE; }

// ffiStruct

void ffiStruct::addMember(string type)
{
	members.push_back(new ffiArgument(type));
}

const int ffiStruct::size() 
{
	int bytes = 0;
	vector<ffiArgument*>::iterator it = members.begin();
	for (it; it != members.end(); it++) bytes += (*it)->size();
	return bytes;
}

ffiArgument& ffiStruct::getMember(int index)
{
	return *members[index];
}
