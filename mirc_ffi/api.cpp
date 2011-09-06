#include "ffi.h"
#define MFUNCTION(procname) int __stdcall procname(HWND mWnd, HWND aWnd, char *data, char *parms, BOOL show, BOOL nopause)

MFUNCTION(ffiopen) 
{
	string idname, imgname;
	istringstream in(data);
	in >> idname >> imgname;
	objects[idname] = new ffiObject(imgname);
	return 1;
}

MFUNCTION(fficlose)
{
	delete objects[data];
	objects.erase(data);
	return 1;
}

MFUNCTION(ffilist)
{
	ostringstream out;
	for (map<string,ffiObject*>::iterator it = objects.begin(); it != objects.end(); it++) {
		out << (*it).first << " ";
	}
	strcpy(data, out.str().c_str());
	return 3;
}

MFUNCTION(ffistruct)
{
	istringstream in(data);
	string idname;
	in >> idname;
	ffiStruct *newstruct = structs[idname] = new ffiStruct;
	string argument;
	while (!in.eof()) {
		in >> argument;
		newstruct->addMember(argument);
	}
	return 1;
}

MFUNCTION(fficreate)
{
	istringstream in(data);
	string funcname, idname;
	in >> idname >> funcname;
	map<string,ffiObject*>::iterator it = objects.find(idname);
	if (it == objects.end()) { return 0; }
	else {
		ffiFunction& func = (*it).second->addFunction(funcname);
		string argument;
		in >> argument;
		func.setReturnType(argument);
		while (!in.eof()) {
			in >> argument;
			func.addArgument(argument);
		}
	}
	return 1;
}

MFUNCTION(fficall)
{
	istringstream in(data);
	string funcname, idname;
	in.width(255);
	in >> idname >> funcname;
	map<string,ffiObject*>::iterator it = objects.find(idname);
	if (it == objects.end()) { return 0; }
	else {
		map<string,ffiFunction*>::iterator fit = (*it).second->functions.find(funcname);
		if (fit == (*it).second->functions.end()) {
			return 0;
		}
		else {
			ffiFunction& func = *(*fit).second;
			if (lastCall != NULL) delete lastCall;
			lastCall = new ffiCall(func);
			vector<string> strArguments;
			vector<ffiArgument*>::iterator ait = func.arguments.begin();
			int offset = 0;
			for (int i = 0; ait != func.arguments.end(); ait++, i++) {
				string strArgument;
				in >> ws;
				int chr = in.peek();
				if (chr == '"') {
					char buf[4096];
					in.get();
					in.getline(buf, 4096, '"');
					strArgument.assign(buf, (size_t)in.gcount());
				}
				else {
					in >> strArgument;
				}
				strArguments.push_back(strArgument);
			}
			lastCall->call(strArguments);
			strcpy(data, lastCall->getReturnValue().c_str());
		}
	}
	return 3;
}

MFUNCTION(fficapture)
{
	if (lastCall == NULL) {
		strcpy(data, "");
	}
	else {
		istringstream in(data);
		int index;
		vector<int> indexes;
		while (!in.eof()) {
			in >> index;
			indexes.push_back(index - 1);
		}
		strcpy(data, lastCall->getCapture(indexes).c_str());
	}
	return 3;
}

typedef struct {
	DWORD  mVersion;
	HWND   mHwnd;
	BOOL   mKeep;
	BOOL   mUnicode;
} LOADINFO;

void __stdcall LoadDll(LOADINFO *info)
{
	info->mKeep = TRUE;
}

int __stdcall UnloadDll(int mTimeout)
{
	if (mTimeout != 1) {
		for (map<string, ffiStruct*>::iterator sit = structs.begin(); sit != structs.end(); sit++)
			delete sit->second;
		for (map<string, ffiObject*>::iterator oit = objects.begin(); oit != objects.end(); oit++)
			delete oit->second;
		delete lastCall;
	}
	return 0;
}
