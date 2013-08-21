#pragma once

#include <Python.h>

#include "types.hpp"
#include "queue.hpp"

#include <boost/shared_ptr.hpp>

namespace py {

class vm_t
{
private:
    vm_t(erlcpp::lpid_t const& pid);
    ~vm_t();

    void run();
    void stop();

	void init_python();
	void release_python();

    static void* thread_run(void * vm);

public :
    struct tasks
    {
        struct load_t
        {
            load_t(erlcpp::binary_t const& file, erlcpp::lpid_t const& caller)
                : file(file), caller(caller)
            {}
            erlcpp::binary_t file;
			erlcpp::lpid_t	 caller;
        };
        struct eval_t
        {
            eval_t(erlcpp::binary_t const& code, erlcpp::lpid_t const& caller)
                : code(code), caller(caller)
            {}
            erlcpp::binary_t code;
			erlcpp::lpid_t	 caller;
        };
        struct call_t
        {
          call_t(erlcpp::atom_t const& module, erlcpp::atom_t const& fun, erlcpp::list_t const& args, erlcpp::lpid_t const& caller)
              : module(module), fun(fun), args(args), caller(caller)
            {};
            erlcpp::atom_t module;
            erlcpp::atom_t fun;
            erlcpp::list_t args;
			erlcpp::lpid_t caller;
        };
        struct resp_t
        {
            resp_t(erlcpp::term_t const& term) : term(term) {}
            erlcpp::term_t term;
        };
        struct quit_t {};
    };
    typedef boost::variant
    <
        tasks::load_t,
        tasks::eval_t,
        tasks::call_t,
        tasks::resp_t,
        tasks::quit_t
    > task_t;

public :

    erlcpp::lpid_t erl_pid() const { return pid_; }

    void add_task(task_t const& task);
    task_t get_task();

    static void destroy(ErlNifEnv* env, void* obj);
    static boost::shared_ptr<vm_t> create(ErlNifResourceType* res_type, erlcpp::lpid_t const& pid);

private :
    erlcpp::lpid_t               pid_;
    ErlNifTid                    tid_;
    queue<task_t>                queue_;
};

}
