/*
 * $Id: lnpext.c,v 1.2 2000/04/06 09:10:18 lsmithso Exp $
 * LegOS LNP Python extension module. 
 *
 * Author: L. Smithson (lsmithson@hare.demon.co.uk)
 *
 * DISCLAIMER
 * You are free to use this code in any way you see fit, subject to
 * the LegOS & Python disclaimers & copyrights. I make no
 * representations about the suitability of this software for any
 * purpose. It is provided "AS-IS" without warranty of any kind,
 * either express or implied. So there.
 *
 * Synchronization:
 * LNP does not provide a blocking read, but operates through
 * callbacks out of a signal handler. To provide blocking, a pthread
 * condvar is used. The signal handler sets the condvar, unblocking
 * the reading thread, which is most likely itself.
 *
 * Threading:
 * Python only delivers signals to the main thread, so if your writing
 * a multi-threaded application, the lnp calls *must* execute in the
 * main thread. 
 *
 * Bugs:
 * The asynch callbacks (areada, iread) don't work. I think this is
 * because the interpreter is not setup in the signal handler. 
 *
 */


#include <pthread.h>

#include "Python.h"
#include "liblnp.h"

static PyObject *ErrorObject;

/*
 * A condvar & mutex used to signal between the lnp signal handler and
 * blocking readers.
 */

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

void condWait() {
  Py_BEGIN_ALLOW_THREADS
  pthread_mutex_lock(&mutex);
  pthread_cond_wait(&cond, &mutex);
  pthread_mutex_unlock(&mutex);
  Py_END_ALLOW_THREADS
}

void condSignal() {
  pthread_cond_signal(&cond);
}

/*
 * Everything is controlled by these variables. They are set by the
 * LNP callbacks and read the Python interface functions.
 */

static PyObject *iReadFuncObj;     /* NULL or a callable */
static char *irData;               /* LNP read data & length */
static int irDataLength;
static int irSrcPort;              /* LNP Addressing read src port */
typedef enum {
  IRCB_NONE, IRCB_ASYNC, IRCB_SYNC
} IrCallback_t;
static IrCallback_t irCallback;    /* Type of callback requested */

/*
 * This is the function called by the interp. to async. call the user
 * supplied function object.
 */
static int irIntHandler() {
  PyObject *result = NULL;
  PyObject *arglist;
  /*
   * Build the callback's args. If a valid source port, add it the arg
   * list
   */
  if (irSrcPort < 0) {
    arglist = Py_BuildValue("(s#)", irData, irDataLength);
  } else {
    arglist = Py_BuildValue("(s#i)", irData, irDataLength, irSrcPort);
  }
  result  = PyEval_CallObject(iReadFuncObj, arglist);
  Py_DECREF(arglist);
  if (!result) {
    PyErr_SetString(PyExc_IOError, "LNP iread callback failed");
    return -1;
  }
  Py_DECREF(result);
  return 0;
}

/*
 * The lnp integrity/addressing callback functions. Called by lnp when
 * a complete & good integrity packet is received.
 * This is called as part of a signal handler. If an async. callback
 * is outstanding, call it via the Python interpreter. If a blocking
 * call is in progress, release the semaphore to unblock the pending
 * read. 
 */

static void lnpIAReadCommon(const unsigned char *data,unsigned char len,
			    unsigned char src) {
  irData = (char *)data;
  irDataLength = len;
  irSrcPort = src;
  if (irCallback == IRCB_ASYNC) {
    if (iReadFuncObj) {
      Py_AddPendingCall((int (*) Py_PROTO((ANY *)))irIntHandler, NULL);
    }
  } else {
    if (irCallback == IRCB_SYNC) {
      condSignal();
    }
  }
}

/*
 * The lnp handlers
 */

static void lnpIRead(const unsigned char *data,unsigned char len) {
  lnpIAReadCommon(data, len, -1);
}

static void lnpARead(const unsigned char *data,unsigned char len,
		     unsigned char src) {
  lnpIAReadCommon(data, len, src);
}


/*
 * The Python lnp extension functions.
 */

static char lnp_iread__doc__[] =
"iread()\n\
\n\
Performs a blocking LNP integrity read, returning the result as a\n\
string. The string  may contain embedded nulls. Any outstanding
asynchronous LNP reads (integrity or addressing) are cancelled.";

static PyObject *
lnp_iread(self, args)
	PyObject *self;	/* Not used */
	PyObject *args;
{
  PyObject *retVal;
  if (!PyArg_ParseTuple(args, ""))
    return NULL;
  irCallback = IRCB_SYNC;
  lnp_integrity_set_handler(lnpIRead);
  /* Block myself until the signal handler releases the sem */
  condWait();
  irCallback = IRCB_NONE;
  retVal = Py_BuildValue("s#", irData, irDataLength);
  return retVal;
}

static char lnp_ireada__doc__[] =
"ireada(callable)\n\
\n\
Performs an asynchronous LNP integrity read. The callable object 'callable' \n\
is called with the string read from the RCX. The string may contain \n\
embedded nulls. Any outstanding asynchronous LNP addressing reads are\n\
cancelled.";

static PyObject *
lnp_ireada(self, args)
	PyObject *self;	/* Not used */
	PyObject *args;
{
  PyObject *result = NULL;
  PyObject *arglist;

  if (!PyArg_ParseTuple(args, "O", &iReadFuncObj))
    return NULL;
  if (!PyCallable_Check(iReadFuncObj)) {
    PyErr_SetString(PyExc_TypeError, "not a callable object");
    return NULL;
  }
  irCallback = IRCB_ASYNC;
  lnp_integrity_set_handler(lnpIRead);
  Py_INCREF(Py_None);
  return Py_None;
}

static char lnp_iwrite__doc__[] =
"iwrite(buf)\n\
\n\
Performs LNP integrity write, sending the buffer buf to the RCX.\n\
The string may contain embedded nulls.";

static PyObject *
lnp_iwrite(self, args)
	PyObject *self;	/* Not used */
	PyObject *args;
{
  char *msg;
  int len;
  if (!PyArg_ParseTuple(args, "s#", &msg, &len))
    return NULL;
  if (lnp_integrity_write(msg, len) != TX_SUCCESS) {
    PyErr_SetString(PyExc_IOError, "Failed to send to IR tower");
    return NULL;
  }
  Py_INCREF(Py_None);
  return Py_None;
}

static char lnp_aread__doc__[] =
"aread(port)\n\
\n\
Performs a blocking LNP addressing read on port 'port', returning\n\
the result as the tuple (buf, port), where buf is the buffer string\n\
read from the RCX and 'port' is the RCX source port number. The\n\
string may contain embedded nulls. Any outstanding asynchronous LNP\n\
reads (integrity or addressing) are cancelled.";

static PyObject *
lnp_aread(self, args)
	PyObject *self;	/* Not used */
	PyObject *args;
{
  int port;
  PyObject *retVal;
  if (!PyArg_ParseTuple(args, "i", &port))
    return NULL;
  if ((port < 0) || (port > 256)) {
    PyErr_SetString(PyExc_OverflowError, "port out of range");
    return NULL;
  }
  irCallback = IRCB_SYNC;
  lnp_addressing_set_handler((unsigned char)port, lnpARead);
  /* Block myself on the signal handler releases the sem */
  condWait();
  irCallback = IRCB_NONE;
  retVal = Py_BuildValue("(s#i)", irData, irDataLength, irSrcPort);
  return retVal;
}

static char lnp_areada__doc__[] =
"areada(port, callable)\n\
\n\
Performs an asynchronous LNP addressing read. The callable object
'callable' is called with the string read from the RCX, and the RCX
port number. The string may contain embedded nulls. Any outstanding
asynchronous LNP integrity reads are cancelled.";

static PyObject *
lnp_areada(self, args)
	PyObject *self;	/* Not used */
	PyObject *args;
{
  int port;
  PyObject *retVal;
  if (!PyArg_ParseTuple(args, "iO", &port, &iReadFuncObj))
    return NULL;
  if ((port < 0) || (port > 256)) {
    PyErr_SetString(PyExc_OverflowError, "port out of range");
    return NULL;
  }
  irCallback = IRCB_ASYNC;
  lnp_addressing_set_handler((unsigned char)port, lnpARead);
  Py_INCREF(Py_None);
  return Py_None;
}

static char lnp_awrite__doc__[] =
"awrite(buf, dst, src)\n\
\n\
Performs LNP addressing write, sending the buffer buf to the RCX.\n\
The string may contain embedded nulls. dst & src are the lnp\n\
destination & source ports.";

static PyObject *
lnp_awrite(self, args)
	PyObject *self;	/* Not used */
	PyObject *args;
{
  char *msg;
  int len;
  int srcport;
  int dstport;
  if (!PyArg_ParseTuple(args, "s#ii", &msg, &len, &dstport, &srcport))
    return NULL;
  if ((dstport < 0) || (dstport > 256) || (srcport < 0) || (srcport > 256)) {
    PyErr_SetString(PyExc_OverflowError, "port out of range");
    return NULL;
  }
  if (lnp_addressing_write(msg, len, (unsigned char)dstport,
			   (unsigned char)srcport) != TX_SUCCESS) {
    PyErr_SetString(PyExc_IOError, "Failed to send to IR tower");
    return NULL;
  }
  Py_INCREF(Py_None);
  return Py_None;
}

/* List of methods defined in the module */

static struct PyMethodDef lnp_methods[] = {
	{"iread",	(PyCFunction)lnp_iread,	METH_VARARGS,	lnp_iread__doc__},
 {"ireada",	(PyCFunction)lnp_ireada,	METH_VARARGS,	lnp_ireada__doc__},
 {"iwrite",	(PyCFunction)lnp_iwrite,	METH_VARARGS,	lnp_iwrite__doc__},
 {"aread",	(PyCFunction)lnp_aread,	METH_VARARGS,	lnp_aread__doc__},
 {"areada",	(PyCFunction)lnp_areada,	METH_VARARGS,	lnp_areada__doc__},
 {"awrite",	(PyCFunction)lnp_awrite,	METH_VARARGS,	lnp_awrite__doc__},
 
	{NULL,	 (PyCFunction)NULL, 0, NULL}		/* sentinel */
};


/* Initialization function for the module (*must* be called initlnp) */

static char lnp_module_documentation[] = 

"This module interfaces to LegOS lnp, using the lnpd package from 
Martin Cornelius (Martin.Cornelius@t-online.de). It allows a python script 
to send & received data via the IR tower to an RCX running LegOS. The
following functions are available:
  iread()                Blocking LNP integrity read\n\
  ireada(callable)       Asynchronous LNP integrity read\n\
  iwrite(buf)            LNP integrity write\n\
  aread(port)            Blocking LNP addressing read\n\
  areada(port, callable) Asynchronous LNP addressing read\n\
  awrite(buf, dst, src)  LNP addressing write 

All reads are mutually exclusive. That is, only one asynchronous or
blocking integrity or addressing read may be outstanding at one time.";


void initlnp() {
  PyObject *m, *d;
  
  /* Create the module and add the functions */
  m = Py_InitModule4("lnp", lnp_methods,
		     lnp_module_documentation,
		     (PyObject*)NULL,PYTHON_API_VERSION);
  
  /* Add some symbolic constants to the module */
  d = PyModule_GetDict(m);
  ErrorObject = PyString_FromString("lnp.error");
  PyDict_SetItemString(d, "error", ErrorObject);
  
  /* Check for errors */
  if (PyErr_Occurred())
    Py_FatalError("can't initialize module lnp");

  /* Initialise LNP */
  irSrcPort = -1;
  iReadFuncObj = 0;
  irCallback = IRCB_NONE;
  if (lnp_init(0, 0, 0, 0, 0)) {
    PyErr_SetString(PyExc_IOError, "Failed to talk to IR tower");
    return;
  }
}
