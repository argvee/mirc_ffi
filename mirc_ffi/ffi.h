#include <windows.h>
#include <map>
#include <vector>
#include <string>
#include <sstream>

using namespace std;

typedef unsigned long ull;
typedef unsigned int (__stdcall VARIFUNC)(void);

class ffiObject;
class ffiFunction;
class ffiArgument;
class ffiStruct;
class ffiCall;
extern map<string, ffiObject*> objects;
extern map<string, ffiStruct*> structs;
extern ffiCall *lastCall;

#define FFI_FLAG_SIGNED  1
#define FFI_FLAG_INTEGER 2
#define FFI_FLAG_DECIMAL 4
#define FFI_FLAG_CHAR    8
#define FFI_FLAG_STRUCT  16
#define FFI_FLAG_POINTER 32
#define FFI_FLAG_VOID    64
#define FFI_FLAG_CAPTURE 128

class ffiArgument {
	int flags;
	int bytes;
public:
	string typeName;
	ffiArgument(string type);
	const int size(bool = false);
	const BOOL isSigned();
	const BOOL isInteger();
	const BOOL isDecimal();
	const BOOL isChar();
	const BOOL isStruct();
	const BOOL isPointer();
	const BOOL isVoid();
	const BOOL isCapture();
};

class ffiStruct {
	vector<ffiArgument*> members;
public:
	void addMember(string type);
	ffiArgument& getMember(int);
	const int size();
};

class ffiCall {
	ffiFunction& function;
	vector<char*> storage;
	vector<char*> captures;
	char *stackData;
	int stackOffset;
	ull returnValue;

	void convertArgument(string&, ffiArgument&);
	string convertArgumentToString(char *data, ffiArgument& argument);
	ffiArgument *getCaptureArgument(int index);
public:
	ffiCall(ffiFunction& func);
	~ffiCall();
	void call(vector<string>);
	string getCapture(int);
	string getCapture(vector<int>);
	string getReturnValue();
};

class ffiFunction {
	ffiObject& owner;
	string functionName;
	ffiArgument *returnType;
	VARIFUNC *funcPtr;
public:
	vector<ffiArgument*> arguments;
	ffiFunction(ffiObject&, string);
	~ffiFunction();
	void addArgument(ffiArgument*);
	void addArgument(string);
	void setReturnType(ffiArgument*);
	void setReturnType(string);

	friend class ffiCall;
};

class ffiObject {
	string imageName;
public:
	HMODULE imageHandle;
	map<string,ffiFunction*> functions;

	ffiObject(string);
	~ffiObject();
	ffiFunction& addFunction(string);
};
