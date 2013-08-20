#include "py.hpp"
#include "utils.hpp"
#include "errors.hpp"
#include "py_utils.hpp"

#include <dlfcn.h>
#include <unistd.h>

namespace py {

char* get_py_error() {
	PyObject *ptype, *pvalue, *ptraceback;
	PyErr_Fetch(&ptype, &pvalue, &ptraceback);

	return PyString_AsString(pvalue);
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
// task handlers:
template <class return_type>
class base_handler : public boost::static_visitor<return_type>
{
public :
    typedef typename boost::static_visitor<return_type>::result_type result_type;

    base_handler(vm_t & vm) : vm_(vm) {};
    virtual return_type operator()(vm_t::tasks::load_t const&) { throw errors::unexpected_msg(); }
    virtual return_type operator()(vm_t::tasks::eval_t const&) { throw errors::unexpected_msg(); }
    virtual return_type operator()(vm_t::tasks::call_t const&) { throw errors::unexpected_msg(); }
    virtual return_type operator()(vm_t::tasks::resp_t const&) { throw errors::unexpected_msg(); }
    virtual return_type operator()(vm_t::tasks::quit_t const&) { throw quit_tag(); }

    vm_t & vm() { return vm_; };
    vm_t const& vm() const { return vm_; }

protected :
    ~base_handler() {}

private :
    vm_t & vm_;
};

/////////////////////////////////////////////////////////////////////////////

class result_handler : public base_handler<erlcpp::term_t>
{
public :
    using base_handler<erlcpp::term_t>::operator();
    result_handler(vm_t & vm) : base_handler<erlcpp::term_t>(vm) {};
    erlcpp::term_t operator()(vm_t::tasks::resp_t const& resp) { return resp.term; }
};

struct call_handler : public base_handler<void>
{
    using base_handler<void>::operator();
    call_handler(vm_t & vm) : base_handler<void>(vm) {};

    // Loading file:
    void operator()(vm_t::tasks::load_t const& load)
    {
        try
        {
            std::string file(load.file.data(), load.file.data() + load.file.size());

			FILE* file_handle = fopen(file.c_str(), "r");

			PyObject* result = PyRun_File(file_handle, file.c_str(), 0, NULL, NULL);

            if (result == NULL )
            {
                erlcpp::tuple_t result(2);
                result[0] = erlcpp::atom_t("error_py");
                result[1] = erlcpp::atom_t(get_py_error());
                send_result_caller(vm(), "moon_response", result, load.caller);
            }
            else
            {
                erlcpp::atom_t result("ok");
                send_result_caller(vm(), "moon_response", result, load.caller);
            }
        }
        catch( std::exception & ex )
        {
            erlcpp::tuple_t result(2);
            result[0] = erlcpp::atom_t("error_py");
            result[1] = erlcpp::atom_t(ex.what());
            send_result_caller(vm(), "moon_response", result, load.caller);
        }
    }

    // Evaluating arbitrary code:
    void operator()(vm_t::tasks::eval_t const& eval)
    {
        try
        {
			PyObject *pyresult = PyRun_String(eval.code.data(), 0, NULL, NULL);

			if (result == NULL) {
				erlcpp::tuple_t result(2);
				result[0] = erlcpp:atom_t("error_py");
				result[1] = erlcpp::atom_t(get_py_error());
                send_result_caller(vm(), "moon_response", result, eval.caller);
            }
            else
            {
                erlcpp::tuple_t result(2);
                result[0] = erlcpp::atom_t("ok");
                result[1] = py:pyvalue_to_term(pyresult);
                send_result_caller(vm(), "moon_response", result, eval.caller);
            }
        }
        catch( std::exception & ex )
        {
            erlcpp::tuple_t result(2);
            result[0] = erlcpp::atom_t("error_py");
            result[1] = erlcpp::atom_t(ex.what());
            send_result_caller(vm(), "moon_response", result, eval.caller);
        }
    }

    // Calling arbitrary function:
    void operator()(vm_t::tasks::call_t const& call)
    {
        try
        {
			PyObject *rootname = PyString_FromString("root");
			PyObject *module = PyImport_Import(rootname);

			PyObject *dict = PyModule_GetDict(module);

			PyObject *func = PyDict_GetItemString(dict, call.fun.c_str());

			PyObject *args = py::term_to_pyvalue(call.args);			

			PyObject *py_result = PyObject_CallObject(func, args);

            if (py_result == NULL)
            {
                erlcpp::tuple_t result(2);
                result[0] = erlcpp::atom_t("error_py");
                result[1] = erlcpp::atom_t(get_py_error());
                send_result_caller(vm(), "moon_response", result, call.caller);
            }
            else
            {
                erlcpp::tuple_t result(2);
                result[0] = erlcpp::atom_t("ok");
                result[1] = pyvalue_to_term(py_result);
                send_result_caller(vm(), "moon_response", result, call.caller);
            }
        }
        catch( std::exception & ex )
        {
            erlcpp::tuple_t result(2);
            result[0] = erlcpp::atom_t("error_py");
            result[1] = erlcpp::atom_t(ex.what());
            send_result_caller(vm(), "moon_response", result, call.caller);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


vm_t::vm_t(erlcpp::lpid_t const& pid)
    : pid_(pid)
{
	Py_Initialize();
//	char ff[256] = {0,};
//	getcwd(ff, 256);
//
//	printf("%s\n", "11111111111111111111111");
//	printf("%s\n", ff);

	void* handle = dlopen("/usr/lib/libpython2.7.so", RTLD_NOW | RTLD_GLOBAL); 	
	//assert(handle != NULL);
}

vm_t::~vm_t()
{
	Py_Finalize();
//     enif_fprintf(stderr, "*** destruct the vm\n");
}

/////////////////////////////////////////////////////////////////////////////

boost::shared_ptr<vm_t> vm_t::create(ErlNifResourceType* res_type, erlcpp::lpid_t const& pid)
{
    enif_fprintf(stdout, "vm_t create------------------------------------------------------------------\n");
    void * buf = enif_alloc_resource(res_type, sizeof(vm_t));
    // TODO: may leak, need to guard agaist
    boost::shared_ptr<vm_t> result(new (buf) vm_t(pid), enif_release_resource);

    if(enif_thread_create(NULL, &result->tid_, vm_t::thread_run, result.get(), NULL) != 0) {
        result.reset();
    }

    return result;
}

void vm_t::destroy(ErlNifEnv* env, void* obj)
{
    static_cast<vm_t*>(obj)->stop();
    static_cast<vm_t*>(obj)->~vm_t();
}

void vm_t::run()
{
    try
    {
        for(;;)
        {
            perform_task<call_handler>(*this);
        }
    }
    catch(quit_tag) {}
    catch(std::exception & ex)
    {
        enif_fprintf(stderr, "*** exception in vm thread: %s\n", ex.what());
    }
    catch(...) {}
}

void vm_t::stop()
{
    queue_.push(tasks::quit_t());
    enif_thread_join(tid_, NULL);
};

void vm_t::add_task(task_t const& task)
{
    queue_.push(task);
}

vm_t::task_t vm_t::get_task()
{
    return queue_.pop();
}

void* vm_t::thread_run(void * vm)
{
    static_cast<vm_t*>(vm)->run();
    return 0;
}

/////////////////////////////////////////////////////////////////////////////

}
