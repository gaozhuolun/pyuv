
static PyObject* PyExc_DNSError;


typedef struct {
    DNSResolver *resolver;
    PyObject *cb;
} ares_cb_data_t;


static void
host_cb(void *arg, int status, int timeouts, struct hostent *hostent)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    ASSERT(arg);

    ares_cb_data_t *data = (ares_cb_data_t*)arg;
    DNSResolver *self = data->resolver;
    PyObject *callback = data->cb;
    ASSERT(self);
    /* Object could go out of scope in the callback, increase refcount to avoid it */
    Py_INCREF(self);

    char ip4[INET_ADDRSTRLEN];
    char ip6[INET6_ADDRSTRLEN];
    char **ptr;

    PyObject *dns_name;
    if (hostent != NULL) {
        dns_name = PyBytes_FromString(hostent->h_name);
    } else {
        Py_INCREF(Py_None);
        dns_name = Py_None;
    }
    PyObject *dns_status = PyLong_FromLong((long) status);
    PyObject *dns_timeouts = PyLong_FromLong((long) timeouts);
    PyObject *dns_aliases = PyList_New(0);
    PyObject *dns_result = PyList_New(0);
    PyObject *tmp;
    PyObject *result;
    int r;

    if (!(dns_status && dns_timeouts && dns_name && dns_aliases && dns_result)) {
        PyErr_NoMemory();
        PyErr_WriteUnraisable(callback);
        goto host_end;
    }

    if (hostent != NULL) {
        for (ptr = hostent->h_aliases; *ptr != NULL; ptr++) {
            if (*ptr != hostent->h_name && strcmp(*ptr, hostent->h_name)) {
                tmp = PyBytes_FromString(*ptr);
                if (tmp == NULL) {
                    break;
                }
                r = PyList_Append(dns_aliases, tmp);
                Py_DECREF(tmp);
                if (r != 0) {
                    break;
                }
            }
        }
        for (ptr = hostent->h_addr_list; *ptr != NULL; ptr++) {
            if (hostent->h_addrtype == AF_INET) {
                inet_ntop(AF_INET, *ptr, ip4, INET_ADDRSTRLEN);
                tmp = PyBytes_FromString(ip4);
            } else {
                inet_ntop(AF_INET6, *ptr, ip6, INET6_ADDRSTRLEN);
                tmp = PyBytes_FromString(ip6);
            }
            if (tmp == NULL) {
                break;
            }
            r = PyList_Append(dns_result, tmp);
            Py_DECREF(tmp);
            if (r != 0) {
                break;
            }
        }
    }

    result = PyObject_CallFunctionObjArgs(callback, self, dns_status, dns_timeouts, dns_name, dns_aliases, dns_result, NULL);
    if (result == NULL) {
        PyErr_WriteUnraisable(callback);
    }
    Py_XDECREF(result);

host_end:

    Py_DECREF(callback);
    PyMem_Free(data);

    Py_DECREF(self);
    PyGILState_Release(gstate);
}


static void
nameinfo_cb(void *arg, int status, int timeouts, char *node, char *service)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    ASSERT(arg);

    ares_cb_data_t *data = (ares_cb_data_t*)arg;
    DNSResolver *self = data->resolver;
    PyObject *callback = data->cb;
    ASSERT(self);
    /* Object could go out of scope in the callback, increase refcount to avoid it */
    Py_INCREF(self);

    PyObject *dns_status = PyLong_FromLong((long) status);
    PyObject *dns_timeouts = PyLong_FromLong((long) timeouts);
    PyObject *dns_node;
    if (node) {
        dns_node = PyBytes_FromString(node);
    } else {
        Py_INCREF(Py_None);
        dns_node = Py_None;
    }
    PyObject *dns_service;
    if (service) {
        dns_service = PyBytes_FromString(service);
    } else {
        Py_INCREF(Py_None);
        dns_service = Py_None;
    }

    if (!(dns_status && dns_timeouts && dns_node && dns_service)) {
        PyErr_NoMemory();
        PyErr_WriteUnraisable(callback);
        goto nameinfo_end;
    }

    PyObject *result = PyObject_CallFunctionObjArgs(callback, self, dns_status, dns_timeouts, dns_node, dns_service, NULL);
    if (result == NULL) {
        PyErr_WriteUnraisable(callback);
    }
    Py_XDECREF(result);

nameinfo_end:

    Py_DECREF(callback);
    PyMem_Free(data);

    Py_DECREF(self);
    PyGILState_Release(gstate);
}


static PyObject *
DNSResolver_func_gethostbyname(DNSResolver *self, PyObject *args, PyObject *kwargs)
{
    PyObject *callback;
    char *name;
    int family = AF_INET;
    ares_cb_data_t *cb_data;

    static char *kwlist[] = {"callback", "name", "family", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Os|i:gethostbyname", kwlist, &callback, &name, &family)) {
        return NULL;
    }

    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    cb_data = (ares_cb_data_t*) PyMem_Malloc(sizeof *cb_data);
    if (!cb_data) {
        return PyErr_NoMemory();
    }

    Py_INCREF(callback);
    cb_data->resolver = self;
    cb_data->cb = callback;

    ares_gethostbyname(self->channel, name, family, &host_cb, (void *)cb_data);

    Py_RETURN_NONE;
}


static PyObject *
DNSResolver_func_gethostbyaddr(DNSResolver *self, PyObject *args, PyObject *kwargs)
{
    PyObject *callback;
    char *name;
    ares_cb_data_t *cb_data;
    struct in_addr addr4;
    struct in6_addr addr6;
    int family;
    int length;
    void *address;

    static char *kwlist[] = {"callback", "name", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Os:gethostbyaddr", kwlist, &callback, &name)) {
        return NULL;
    }

    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    if (inet_pton(AF_INET, name, &addr4) == 1) {
        family = AF_INET;
        length = 4;
        address = (void *)&addr4;
    } else if (inet_pton(AF_INET6, name, &addr6) == 1) {
        family = AF_INET6;
        length = 16;
        address = (void *)&addr6;
    } else {
        PyErr_SetString(PyExc_ValueError, "invalid IP address");
        return NULL;
    }

    cb_data = (ares_cb_data_t*) PyMem_Malloc(sizeof *cb_data);
    if (!cb_data) {
        return PyErr_NoMemory();
    }

    Py_INCREF(callback);
    cb_data->resolver = self;
    cb_data->cb = callback;

    ares_gethostbyaddr(self->channel, address, length, family, &host_cb, (void *)cb_data);

    Py_RETURN_NONE;
}


static PyObject *
DNSResolver_func_getnameinfo(DNSResolver *self, PyObject *args, PyObject *kwargs)
{
    PyObject *callback;
    ares_cb_data_t *cb_data;
    struct in_addr addr4;
    struct in6_addr addr6;
    struct sockaddr *sa;
    struct sockaddr_in sa4;
    struct sockaddr_in6 sa6;
    char *name;
    int port;
    int flags;
    int length;

    static char *kwlist[] = {"callback", "name", "port", "flags", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Osii:getnameinfo", kwlist, &callback, &name, &port, &flags)) {
        return NULL;
    }

    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    if (port < 0 || port > 65536) {
        PyErr_SetString(PyExc_ValueError, "port must be between 0 and 65536");
        return NULL;
    }

    if (inet_pton(AF_INET, name, &addr4) == 1) {
        sa4 = uv_ip4_addr(name, port);
        sa = (struct sockaddr *)&sa4;
        length = sizeof(struct sockaddr_in);
    } else if (inet_pton(AF_INET6, name, &addr6) == 1) {
        sa6 = uv_ip6_addr(name, port);
        sa = (struct sockaddr *)&sa6;
        length = sizeof(struct sockaddr_in6);
    } else {
        PyErr_SetString(PyExc_ValueError, "invalid IP address");
        return NULL;
    }

    cb_data = (ares_cb_data_t*) PyMem_Malloc(sizeof *cb_data);
    if (!cb_data) {
        return PyErr_NoMemory();
    }

    Py_INCREF(callback);
    cb_data->resolver = self;
    cb_data->cb = callback;

    ares_getnameinfo(self->channel, sa, length, flags, &nameinfo_cb, (void *)cb_data);
    Py_RETURN_NONE;
}


static int
set_dns_servers(DNSResolver *self, PyObject *value)
{
    struct ares_addr_node *servers;
    char *server;
    int i;
    int length;
    int ret = 0;

    PyObject *server_list = value;
    PyObject *item;

    if (!PyList_Check(server_list)) {
        PyErr_SetString(PyExc_TypeError, "servers argument must be a list");
	return -1;
    }

    length = PyList_Size(server_list);
    servers = PyMem_Malloc(sizeof(struct ares_addr_node) * length);

    for (i = 0; i < length; i++) {
        item = PyList_GetItem(server_list, i);
        if (!item) {
            ret = -1;
            goto servers_set_end;
        }

        server = PyString_AsString(item);
        if (!server) {
            ret = -1;
            goto servers_set_end;
        }

        if (inet_pton(AF_INET, server, &servers[i].addr.addr4) == 1) {
            servers[i].family = AF_INET;
        } else if (inet_pton(AF_INET6, server, &servers[i].addr.addr6) == 1) {
            servers[i].family = AF_INET6;
        } else {
            PyErr_SetString(PyExc_ValueError, "invalid IP address");
            ret = -1;
            goto servers_set_end;
        }

        if (i > 0) {
            servers[i-1].next = &servers[i];
        }
    }

    if (length > 0) {
        servers[length-1].next = NULL;
    } else {
        servers = NULL;
    }

    int r = ares_set_servers(self->channel, servers);
    if (r != 0) {
        PyErr_SetString(PyExc_DNSError, "error c-ares library options");
        ret = -1;
    }

servers_set_end:

    PyMem_Free(servers);
    return ret;
}


static PyObject *
DNSResolver_servers_get(DNSResolver *self, void *closure)
{
    char ip4[INET_ADDRSTRLEN];
    char ip6[INET6_ADDRSTRLEN];
    struct ares_addr_node *server;
    struct ares_addr_node *servers;

    PyObject *server_list;
    PyObject *tmp;

    server_list = PyList_New(0);
    if (!server_list) {
        PyErr_NoMemory();
        return NULL;
    }

    int r = ares_get_servers(self->channel, &servers);
    if (r != 0) {
        PyErr_SetString(PyExc_DNSError, "error getting c-ares nameservers");
        return NULL;
    }

    for (server = servers; server != NULL; server = server->next) {
        if (server->family == AF_INET) {
            inet_ntop(AF_INET, &(server->addr.addr4), ip4, INET_ADDRSTRLEN);
            tmp = PyBytes_FromString(ip4);
        } else {
            inet_ntop(AF_INET6, &(server->addr.addr6), ip6, INET6_ADDRSTRLEN);
            tmp = PyBytes_FromString(ip6);
        }
        if (tmp == NULL) {
            break;
        }
        r = PyList_Append(server_list, tmp);
        Py_DECREF(tmp);
        if (r != 0) {
            break;
        }
    }

    return server_list;
}


static int
DNSResolver_servers_set(DNSResolver *self, PyObject *value, void *closure)
{
    return set_dns_servers(self, value);
}


static int
DNSResolver_tp_init(DNSResolver *self, PyObject *args, PyObject *kwargs)
{
    Loop *loop;
    PyObject *tmp = NULL;
    PyObject *servers = NULL;

    static char *kwlist[] = {"loop", "servers", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!|O:__init__", kwlist, &LoopType, &loop, &servers)) {
        return -1;
    }

    tmp = (PyObject *)self->loop;
    Py_INCREF(loop);
    self->loop = loop;
    Py_XDECREF(tmp);

    int r = ares_library_init(ARES_LIB_INIT_ALL);
    if (r != 0) {
        PyErr_SetString(PyExc_DNSError, "error initializing c-ares library");
        return -1;
    }

    struct ares_options options;
    int optmask;

    optmask = ARES_OPT_FLAGS;
    options.flags = ARES_FLAG_USEVC;

    r = uv_ares_init_options(SELF_LOOP, &self->channel, &options, optmask);
    if (r) {
        PyErr_SetString(PyExc_DNSError, "error c-ares library options");
        return -1;
    }

    if (servers) {
        return set_dns_servers(self, servers);
    }

    return 0;
}


static PyObject *
DNSResolver_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    DNSResolver *self = (DNSResolver *)PyType_GenericNew(type, args, kwargs);
    if (!self) {
        return NULL;
    }
    return (PyObject *)self;
}


static int
DNSResolver_tp_traverse(DNSResolver *self, visitproc visit, void *arg)
{
    Py_VISIT(self->loop);
    return 0;
}


static int
DNSResolver_tp_clear(DNSResolver *self)
{
    Py_CLEAR(self->loop);
    return 0;
}


static void
DNSResolver_tp_dealloc(DNSResolver *self)
{
    uv_ares_destroy(SELF_LOOP, self->channel);
    DNSResolver_tp_clear(self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}


static PyMethodDef
DNSResolver_tp_methods[] = {
    { "gethostbyname", (PyCFunction)DNSResolver_func_gethostbyname, METH_VARARGS|METH_KEYWORDS, "Gethostbyname" },
    { "gethostbyaddr", (PyCFunction)DNSResolver_func_gethostbyaddr, METH_VARARGS|METH_KEYWORDS, "Gethostbyaddr" },
    { "getnameinfo", (PyCFunction)DNSResolver_func_getnameinfo, METH_VARARGS|METH_KEYWORDS, "Getnameinfo" },
    { NULL }
};


static PyMemberDef DNSResolver_tp_members[] = {
    {"loop", T_OBJECT_EX, offsetof(DNSResolver, loop), READONLY, "Loop where this DNSResolver is running on."},
    {NULL}
};


static PyGetSetDef DNSResolver_tp_getsets[] = {
    {"servers", (getter)DNSResolver_servers_get, (setter)DNSResolver_servers_set, "DNS server list", NULL},
    {NULL}
};


static PyTypeObject DNSResolverType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pyuv.DNSResolver",                                         /*tp_name*/
    sizeof(DNSResolver),                                            /*tp_basicsize*/
    0,                                                              /*tp_itemsize*/
    (destructor)DNSResolver_tp_dealloc,                             /*tp_dealloc*/
    0,                                                              /*tp_print*/
    0,                                                              /*tp_getattr*/
    0,                                                              /*tp_setattr*/
    0,                                                              /*tp_compare*/
    0,                                                              /*tp_repr*/
    0,                                                              /*tp_as_number*/
    0,                                                              /*tp_as_sequence*/
    0,                                                              /*tp_as_mapping*/
    0,                                                              /*tp_hash */
    0,                                                              /*tp_call*/
    0,                                                              /*tp_str*/
    0,                                                              /*tp_getattro*/
    0,                                                              /*tp_setattro*/
    0,                                                              /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC|Py_TPFLAGS_BASETYPE,      /*tp_flags*/
    0,                                                              /*tp_doc*/
    (traverseproc)DNSResolver_tp_traverse,                          /*tp_traverse*/
    (inquiry)DNSResolver_tp_clear,                                  /*tp_clear*/
    0,                                                              /*tp_richcompare*/
    0,                                                              /*tp_weaklistoffset*/
    0,                                                              /*tp_iter*/
    0,                                                              /*tp_iternext*/
    DNSResolver_tp_methods,                                         /*tp_methods*/
    DNSResolver_tp_members,                                         /*tp_members*/
    DNSResolver_tp_getsets,                                         /*tp_getsets*/
    0,                                                              /*tp_base*/
    0,                                                              /*tp_dict*/
    0,                                                              /*tp_descr_get*/
    0,                                                              /*tp_descr_set*/
    0,                                                              /*tp_dictoffset*/
    (initproc)DNSResolver_tp_init,                                  /*tp_init*/
    0,                                                              /*tp_alloc*/
    DNSResolver_tp_new,                                             /*tp_new*/
};


