#include <Python.h>
#include "errors.hpp"
#include "py_utils.hpp"

#include <string>

namespace py {

/////////////////////////////////////////////////////////////////////////////
//

class py_dummy_t : public boost::static_visitor<void>
{
public :
    typedef py_dummy_t self_t;
    py_dummy_t() : py_value(NULL) {};

	PyObject* py_value;

    void operator()(int32_t const& value)
    {
		py_value = Py_BuildValue("i", value);
    }
    void operator()(int64_t const& value)
    {
		py_value = Py_BuildValue("l", value);
    }
    void operator()(double const& value)
    {
		py_value = Py_BuildValue("d", value);
    }
    void operator()(erlcpp::num_t const& value)
    {
        self_t & self = *this;
        boost::apply_visitor(self, value);
    }
    void operator()(erlcpp::lpid_t const& value)
    {
        throw errors::unsupported_type("pid_not_convert_to_binary");
    }
    void operator()(erlcpp::atom_t const& value)
    {
		if (value == "true")
		{
			py_value = Py_True;
		}
		else if (value == "false")
		{
			py_value = Py_False;
		}
		else if (value == "none" or value == "nil" or value == "null")
		{
			py_value = Py_None;
		}
		else
		{
			py_value = Py_BuildValue("s#", value.c_str(), value.size());
		}
    }

    void operator()(erlcpp::binary_t const& value)
    {
		py_value = Py_BuildValue("s#", value.c_str(), value.size());
    }

    void operator()(erlcpp::list_t const& value)
    {
		py_value = Py_BuildValue("[]");
        erlcpp::list_t::const_iterator i, end = value.end();
        for( i = value.begin(); i != end; ++i )
        {
			py_dummy_t p();
			boost::apply_visitor(p, value);
			PyList_Append(py_value, p.py_value);
        }
    }

	//放弃tuple数据结构，用tuple来表示hashtable,数目也就必须是2的倍数, 不是的话
    void operator()(erlcpp::tuple_t const& value)
    {
		if (value.size % 2 == 0) {
			PyObject *dict = PyDict_New();
       	    for( erlcpp::tuple_t::size_type i = 0, end = value.size(); i != end; i=i+2 )
       	    {
				py_dummy_t k();
				py_dummy_t v();
       	        boost::apply_visitor(k, value[i]);
				boost::apply_visitor(v, value[i+1]);

				int result = PyDict_SetItem(py_value, k.py_value, v.py_value);

				if (result == -1) {
					throw errors::unsupported_type("pydict_setitem_fail");
				}
       	    }
		} else {
        	throw errors::unsupported_type("tuple_not_pairs");
		}
   }

};

/////////////////////////////////////////////////////////////////////////////
//
//

PyObject* term_to_pyvalue(erlcpp::term_t const& val)
{
    py_dummy_t p();
    boost::apply_visitor(p, val);
	
	return p.py_value
}

erlcpp::term_t pyvalue_to_term(PyObject* pyvalue) {
	char* tp_name = pyvalue->ob_type->tp_name;

	using namespace std;

	string type_name(tp_name);

	if type_name == "int" {
		long value = PyInt_AsLong(pyvalue);

		if (value == -1) {
			throw unsupported_type("PyInt_AsLong_Fail");
		}

		return erlcpp::num_t((int32_t)value);

	} else if type_name == "long" {
		PY_LONG_LONG value = PyLong_AsLongLongAndOverflow(pyvalue);

		if (value == -1) {
			throw unsupported_type("PyLong_AsLongLongAndOverflow_Fail");
		}

		return erlcpp::num_t((int64_t)value);
	} else if type_name == "float" {
		double value = PyFloat_AsDouble(pyvalue);

		if (value < 0) {
			throw unsupported_type("PyFloat_AsDouble_Fail");
		}
		return erlcpp::num_t(value);

	} else if type_name == "str" {
		Py_ssize_t len = PyString_Size(pyvalue);
		const char * val = PyString_AsString(pyvalue);
		return erlcpp::binary_t(erlcpp::binary_t::data_t(val, val+((int)len)));

	} else if type_name == "bool" {
		if (pyvalue == PY_True) {
			return erlcpp::atom_t("true");
		} else {
			return erlcpp::atom_t("false")
		}
	} else if type_name == "NoneType" {
		return erlcpp::atom_t("none");

	} else if type_name == "list" {
		erlcpp::list_t result;
		int list_size = (int)PyList_Size(pyvalue)

			for(int32_t index = 0; index < list_size; ++index)
			{
				erlcpp::term_t val = pyvalue_to_term(PyList_GetItem(pyvalue, index));

				result.push_back(val);

			}
		return result;

	} else if type_name == "dict" {
		PyObject* key = PyDict_Keys(pyvalue);
		PyObject* value = PyDict_Values(pyvalue);	

		Py_ssize_t size = PyList_Size(key);

		erlcpp::tuple_t result(size * 2);

		for (int32_t index = 0; index < size; ++index) {
			result[2*index] = pyvalue_to_term(PyList_GetItem(key, index))
			result[2*index+1] = pyvalue_to_term(PyList_GetItem(value, index))
		}

		return result;
	} else {
    	throw errors::unsupported_type(tpname);
	}
}


/////////////////////////////////////////////////////////////////////////////

} // namespace lua
