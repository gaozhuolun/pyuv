
static void
threadpool_work_cb(uv_work_t *req)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    WorkRequest *pyreq;
    PyObject *result;

    ASSERT(req);
    pyreq = (WorkRequest *)req->data;

    result = PyObject_CallFunctionObjArgs(pyreq->work_cb, NULL);
    if (result == NULL) {
        print_uncaught_exception();
    }

    PyGILState_Release(gstate);
}


static void
threadpool_done_cb(uv_work_t *req, int status)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    uv_err_t err;
    WorkRequest *pyreq;
    Loop *loop;
    PyObject *result, *errorno;

    ASSERT(req);
    loop = (Loop *)req->loop->data;
    pyreq = (WorkRequest *)req->data;

    if (pyreq->done_cb) {
        if (status < 0) {
            err = uv_last_error(req->loop);
            errorno = PyInt_FromLong((long)err.code);
        } else {
            errorno = Py_None;
            Py_INCREF(Py_None);
        }

        result = PyObject_CallFunctionObjArgs(pyreq->done_cb, errorno, NULL);
        if (result == NULL) {
            handle_uncaught_exception(loop);
        }
        Py_XDECREF(result);
        Py_DECREF(errorno);
    }

    Py_DECREF(pyreq->work_cb);
    Py_XDECREF(pyreq->done_cb);
    pyreq->work_cb = NULL;
    pyreq->done_cb = NULL;
    ((Request *)pyreq)->req = NULL;
    Py_DECREF(pyreq);
    PyMem_Free(req);

    PyGILState_Release(gstate);
}


static PyObject *
ThreadPool_func_queue_work(ThreadPool *self, PyObject *args)
{
    int r;
    uv_work_t *req;
    WorkRequest *pyreq;
    PyObject *work_cb, *done_cb;

    req = NULL;
    pyreq = NULL;
    done_cb = NULL;

    if (!PyArg_ParseTuple(args, "O|O:queue_work", &work_cb, &done_cb)) {
        return NULL;
    }

    if (!PyCallable_Check(work_cb)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    if (done_cb != NULL && !PyCallable_Check(done_cb)) {
        PyErr_SetString(PyExc_TypeError, "done_cb must be a callable");
        return NULL;
    }

    req = PyMem_Malloc(sizeof(uv_work_t));
    if (!req) {
        PyErr_NoMemory();
        goto error;
    }

    pyreq = (WorkRequest *)PyObject_CallObject((PyObject *)&WorkRequestType, NULL);
    if (!pyreq) {
        PyErr_NoMemory();
        goto error;
    }
    Py_INCREF(pyreq);
    ((Request *)pyreq)->req = (uv_req_t *)req;
    pyreq->work_cb = work_cb;
    pyreq->done_cb = done_cb;

    Py_INCREF(work_cb);
    Py_XINCREF(done_cb);
    req->data = (void *)pyreq;

    r = uv_queue_work(UV_LOOP(self), req, threadpool_work_cb, threadpool_done_cb);
    if (r != 0) {
        RAISE_UV_EXCEPTION(UV_LOOP(self), PyExc_ThreadPoolError);
        Py_DECREF(pyreq);
        goto error;
    }

    return (PyObject *)pyreq;

error:
    PyMem_Free(req);
    Py_XDECREF(pyreq);
    Py_DECREF(work_cb);
    Py_XDECREF(done_cb);
    return NULL;
}


static int
ThreadPool_tp_init(ThreadPool *self, PyObject *args, PyObject *kwargs)
{
    Loop *loop;
    PyObject *tmp = NULL;

    if (!PyArg_ParseTuple(args, "O!:__init__", &LoopType, &loop)) {
        return -1;
    }

    tmp = (PyObject *)self->loop;
    Py_INCREF(loop);
    self->loop = loop;
    Py_XDECREF(tmp);

    return 0;
}


static PyObject *
ThreadPool_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    ThreadPool *self = (ThreadPool *)PyType_GenericNew(type, args, kwargs);
    if (!self) {
        return NULL;
    }
    return (PyObject *)self;
}


static int
ThreadPool_tp_traverse(ThreadPool *self, visitproc visit, void *arg)
{
    Py_VISIT(self->loop);
    return 0;
}


static int
ThreadPool_tp_clear(ThreadPool *self)
{
    Py_CLEAR(self->loop);
    return 0;
}


static void
ThreadPool_tp_dealloc(ThreadPool *self)
{
    ThreadPool_tp_clear(self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}


static PyMethodDef
ThreadPool_tp_methods[] = {
    { "queue_work", (PyCFunction)ThreadPool_func_queue_work, METH_VARARGS, "Queue the given function to be run in the thread pool." },
    { NULL }
};


static PyMemberDef ThreadPool_tp_members[] = {
    {"loop", T_OBJECT_EX, offsetof(ThreadPool, loop), READONLY, "Loop where this ThreadPool is running on."},
    {NULL}
};


static PyTypeObject ThreadPoolType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pyuv.ThreadPool",                                              /*tp_name*/
    sizeof(ThreadPool),                                             /*tp_basicsize*/
    0,                                                              /*tp_itemsize*/
    (destructor)ThreadPool_tp_dealloc,                              /*tp_dealloc*/
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,                        /*tp_flags*/
    0,                                                              /*tp_doc*/
    (traverseproc)ThreadPool_tp_traverse,                           /*tp_traverse*/
    (inquiry)ThreadPool_tp_clear,                                   /*tp_clear*/
    0,                                                              /*tp_richcompare*/
    0,                                                              /*tp_weaklistoffset*/
    0,                                                              /*tp_iter*/
    0,                                                              /*tp_iternext*/
    ThreadPool_tp_methods,                                          /*tp_methods*/
    ThreadPool_tp_members,                                          /*tp_members*/
    0,                                                              /*tp_getsets*/
    0,                                                              /*tp_base*/
    0,                                                              /*tp_dict*/
    0,                                                              /*tp_descr_get*/
    0,                                                              /*tp_descr_set*/
    0,                                                              /*tp_dictoffset*/
    (initproc)ThreadPool_tp_init,                                   /*tp_init*/
    0,                                                              /*tp_alloc*/
    ThreadPool_tp_new,                                              /*tp_new*/
};


