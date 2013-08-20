#include <Python.h>

int main(int argc, char** argv) {
	Py_Initialize();

	PyRun_SimpleString("import sys; print sys.path\n");

	Py_Finalize();

	return 0;
}
