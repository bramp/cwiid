/*
 * Copyright (C) 2007 Justin M. Tulloss <jmtulloss@gmail.com>
 *
 * Interface from Python to libcwiid
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301  USA
 *
 * ChangeLog:
 * 2007-06-05 L. Donnie Smith <cwiid@abstrakraft.org>
 * * removed Wiimote_FromC function
 * * added bdaddr argument to Wiimote.init
 * * overloaded Wiimote.init to accept CObject (existing wiimote),
 *   and logic to avoid closing it on dealloc
 *
 * 2007-06-01 L. Donnie Smith <cwiid@abstrakraft.org>
 * * added Wiimote_FromC
 * * added get_acc_cal
 *
 * 2007-05-27 Arthur Peters <amp@singingwizard.org>
 * * removed set_mesg_callback from methods table
 *
 * 2007-05-22 L. Donnie Smith <cwiid@abstrakraft.org>
 * * changed disconnect to close
 * * replaced command with attributes for rpt_mode, rumble, led,
 *   added request_status method
 * * fixed memory leak in get_mesg
 * * added function names to argument parsing errors
 * * changed to processMesg to ConvertMesgArray with global visibility
 *
 * 2007-05-15 L. Donnie Smith <cwiid@abstrakraft.org>
 * * revised message types
 * * revised argument/keylist parsing
 *
 * 2007-05-14 L. Donnie Smith <cwiid@abstrakraft.org>
 * * moved Wiimote class to separate file
 */

#include "Python.h"
#include "structmember.h"
#include <errno.h>
#include <bluetooth/bluetooth.h>
#include "cwiid.h"

typedef struct {
	PyObject_HEAD
	cwiid_wiimote_t *wiimote;
	PyObject *callback;
	char close_on_dealloc;
} Wiimote;

/* method prototypes */
static PyObject *
	Wiimote_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
static void Wiimote_dealloc(Wiimote *self);
static int Wiimote_init(Wiimote *self, PyObject *args, PyObject *kwds);
static PyObject *Wiimote_close(Wiimote *self);

static PyObject *Wiimote_enable(Wiimote *self, PyObject *args, PyObject *kwds);
static PyObject *
	Wiimote_disable(Wiimote *self, PyObject *args, PyObject *kwds);

static int
	Wiimote_set_mesg_callback(Wiimote *self, PyObject *args, void *closure);
static PyObject *Wiimote_get_mesg(Wiimote *self);
static PyObject *Wiimote_get_state(Wiimote *self, void *closure);
static PyObject *Wiimote_get_acc_cal(Wiimote *self, PyObject *args,
                                     PyObject *kwds);

static PyObject *Wiimote_request_status(Wiimote *self);
static int Wiimote_set_led(Wiimote *self, PyObject *PyLed, void *closure);
static int
	Wiimote_set_rumble(Wiimote *self, PyObject *PyRumble, void *closure);
static int
	Wiimote_set_rpt_mode(Wiimote *self, PyObject *PyRptMode, void *closure);

static PyObject *Wiimote_read(Wiimote *self, PyObject *args, PyObject *kwds);
static PyObject *Wiimote_write(Wiimote *self, PyObject *args, PyObject *kwds);

/* helper prototypes */
static cwiid_mesg_callback_t CallbackBridge;
PyObject *ConvertMesgArray(int mesg_count, union cwiid_mesg mesg[]);

static PyMethodDef Wiimote_Methods[] =
{
	{"close", (PyCFunction)Wiimote_close, METH_NOARGS,
	 "close wiimote connection"},
	{"enable", (PyCFunction)Wiimote_enable, METH_VARARGS | METH_KEYWORDS,
	 "enable flags on wiimote"},
	{"disable", (PyCFunction)Wiimote_disable, METH_VARARGS | METH_KEYWORDS,
	 "disable flags on wiimote"},
	{"get_mesg", (PyCFunction)Wiimote_get_mesg, METH_NOARGS,
	 "blocking call to get messages"},
	{"get_acc_cal", (PyCFunction)Wiimote_get_acc_cal,
	 METH_VARARGS | METH_KEYWORDS, "get accelerometer calibration"},
	{"request_status", (PyCFunction)Wiimote_request_status, METH_NOARGS,
	 "request status message"},
	{"read", (PyCFunction)Wiimote_read, METH_VARARGS | METH_KEYWORDS,
	 "read from wiimote"},
	{"write", (PyCFunction)Wiimote_write, METH_VARARGS | METH_KEYWORDS,
	 "write to wiimote"},
	{NULL, NULL, 0, NULL}
};

static PyGetSetDef Wiimote_GetSet[] = {
	{"state", (getter)Wiimote_get_state, NULL, "Wiimote state", NULL},
	{"mesg_callback", NULL, (setter)Wiimote_set_mesg_callback,
	 "Wiimote message callback", NULL},
	{"led", NULL, (setter)Wiimote_set_led, "Wiimote led state", NULL},
	{"rumble", NULL, (setter)Wiimote_set_rumble, "Wiimote rumble state", NULL},
	{"rpt_mode", NULL, (setter)Wiimote_set_rpt_mode, "Wiimote report mode",
	 NULL},
	{NULL, NULL, NULL, NULL, NULL}
};

PyTypeObject Wiimote_Type = {
	PyObject_HEAD_INIT(NULL)
	0,						/* ob_size */
	"cwiid.Wiimote",		/* tp_name */
	sizeof(Wiimote),		/* tp_basicsize */
	0,						/* tp_itemsize */
	(destructor)Wiimote_dealloc,	/* tp_dealloc */
	0,						/* tp_print */
	0,						/* tp_getattr */
	0,						/* tp_setattr */
	0,						/* tp_compare */
	0,						/* tp_repr */
	0,						/* tp_as_number */
	0,						/* tp_as_sequence */
	0,						/* tp_as_mapping */
	0,						/* tp_hash */
	0,						/* tp_call */
	0,						/* tp_str */
	0,						/* tp_getattro */
	0,						/* tp_setattro */
	0,						/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,	/* tp_flags */
	"cwiid c-python interface",	/* tp_doc */
	0,						/* tp_traverse */
	0,						/* tp_clear */
	0,						/* tp_richcompare */
	0,						/* tp_weaklistoffset */
	0,						/* tp_iter */
	0,						/* tp_iternext */
	Wiimote_Methods,		/* tp_methods */
	0,						/* tp_members */
	Wiimote_GetSet,			/* tp_getset */
	0,						/* tp_base */
	0,						/* tp_dict */
	0,						/* tp_descr_get */
	0,						/* tp_descr_set */
	0,						/* tp_dictoffset */
	(initproc)Wiimote_init,	/* tp_init */
	0,						/* tp_alloc */
	Wiimote_new,			/* tp_new */
};

/* Allocate and deallocate functions */
static PyObject *
	Wiimote_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	Wiimote* self;

	if (!(self = (Wiimote *) type->tp_alloc(type, 0))) {
		return NULL;
	}

	self->wiimote = NULL;
	Py_INCREF(self->callback = Py_None);
	self->close_on_dealloc = 0;

	return (PyObject*) self;
}

static void Wiimote_dealloc(Wiimote *self)
{
	if (self->close_on_dealloc && self->wiimote) {
		cwiid_close(self->wiimote);
	}
	Py_XDECREF(self->callback);
	self->ob_type->tp_free((PyObject *)self);
}

static int cwiid_start(Wiimote *self, int flags)
{
	cwiid_wiimote_t *wiimote;

	/* Set up wiimote */

	/* keep pyobject with wiimote */
	cwiid_set_data(wiimote,self);
	/* keep wiimote with pyobject */
	self->wiimote = wiimote;
	return 0;
}

static int Wiimote_init(Wiimote* self, PyObject* args, PyObject *kwds)
{
	static char *kwlist[] = {"bdaddr", "flags", NULL};
	PyObject *PyObj;
	cwiid_wiimote_t *wiimote = NULL;
	char *str_bdaddr = NULL;
	bdaddr_t bdaddr;
	int flags = 0;

	/* Overloaded function - if a single CObject is passed in, it's
	 * an existing CObject.  Otherwise, create a new one */
	if (PyTuple_Size(args) == 1) {
		PyObj = PyTuple_GET_ITEM(args, 0);
		if (PyCObject_Check(PyObj)) {
			wiimote = PyCObject_AsVoidPtr(PyObj);
			self->close_on_dealloc = 0;
		}
	}

	if (!wiimote) {
		if (!PyArg_ParseTupleAndKeywords(args, kwds, "|si:cwiid.Wiimote.init",
		                                 kwlist, &str_bdaddr, &flags)) {
			return -1;
		}

		if (str_bdaddr) {
			if (str2ba(str_bdaddr, &bdaddr)) {
				PyErr_SetString(PyExc_IOError, "bad bdaddr");
				return -1;
			}
		}
		else {
			bdaddr = *BDADDR_ANY;
		}

		if (!(wiimote = cwiid_open(&bdaddr, flags))) {
			PyErr_SetString(PyExc_IOError, "Could not open wiimote");
			return -1;
		}
		else {
			self->close_on_dealloc = 1;
		}
	}

	cwiid_set_data(wiimote, self);
	self->wiimote = wiimote;
	return 0;
}

static PyObject *Wiimote_close(Wiimote *self)
{
	if (cwiid_close(self->wiimote)) {
		PyErr_SetString(PyExc_IOError, "Wiimote close error");
		self->wiimote = NULL;
		return NULL;
	}
	self->wiimote = NULL;

	Py_RETURN_NONE;
}

static PyObject *Wiimote_enable(Wiimote *self, PyObject *args, PyObject *kwds)
{
	static char *kwlist[] = {"flags", NULL};
	int flags;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i:cwiid.Wiimote.enable",
	                                 kwlist, &flags)) {
		return NULL;
	}

	if (cwiid_enable(self->wiimote, flags)) {
		PyErr_SetString(PyExc_IOError, "cwiid_enable error");
		return NULL;
	}

	Py_RETURN_NONE;
}

static PyObject *Wiimote_disable(Wiimote *self, PyObject *args, PyObject *kwds)
{
	static char *kwlist[] = {"flags", NULL};
	int flags;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i:cwiid.Wiimote.disable",
	                                 kwlist, &flags)) {
		return NULL;
	}

	if (cwiid_disable(self->wiimote, flags)) {
		PyErr_SetString(PyExc_IOError, "cwiid_disable error");
		return NULL;
	}

	Py_RETURN_NONE;
}

static int
	Wiimote_set_mesg_callback(Wiimote *self, PyObject *NewCallback,
	                          void *closure)
{
	PyObject *OldCallback;

	if (!PyCallable_Check(NewCallback)) {
		PyErr_SetString(PyExc_TypeError, "callback must be callable!");
	}
	OldCallback = self->callback;

	if ((OldCallback == Py_None) && (NewCallback != Py_None)) {
		if (cwiid_set_mesg_callback(self->wiimote, CallbackBridge)) {
			PyErr_SetString(PyExc_IOError, "set callback error");
			return -1;
		}
	}
	else if ((OldCallback != Py_None) && (NewCallback == Py_None)) {
		if (cwiid_set_mesg_callback(self->wiimote, NULL)) {
			PyErr_SetString(PyExc_IOError, "set callback error");
			return -1;
		}
	}

	Py_INCREF(NewCallback);
	Py_DECREF(OldCallback);
	self->callback = NewCallback;

	return 0;
}

static PyObject *Wiimote_get_mesg(Wiimote *self)
{
	union cwiid_mesg *mesg;
	int mesg_count;
	struct timespec t;
	PyObject *PyMesg;

	if (cwiid_get_mesg(self->wiimote, &mesg_count, &mesg, &t)) {
		if (errno == EAGAIN) {
			Py_RETURN_NONE;
		}
		else {
			PyErr_SetString(PyExc_IOError, "get_mesg error");
			return NULL;
		}
	}

	PyMesg = ConvertMesgArray(mesg_count, mesg);

	free(mesg);

	return PyMesg;
}

static PyObject *Wiimote_get_state(Wiimote* self, void *closure)
{
	struct cwiid_state state;
	PyObject *PyState;

	if (cwiid_get_state(self->wiimote, &state)) {
		PyErr_SetString(PyExc_IOError, "get state error");
		return NULL;
	}

	PyState = Py_BuildValue("{s:B,s:B,s:B,s:B,s:i,s:i}",
	                        "rpt_mode", state.rpt_mode,
	                        "led", state.led,
	                        "rumble", state.rumble,
	                        "battery", state.battery,
	                        "ext_type", state.ext_type,
	                        "error", state.error);

	if (state.rpt_mode & CWIID_RPT_BTN) {
		PyObject *PyBtn = Py_BuildValue("I", state.buttons);
		if (!PyBtn) {
			Py_DECREF(PyState);
			return NULL;
		}
		if (PyDict_SetItemString(PyState, "buttons", PyBtn)) {
			Py_DECREF(PyState);
			Py_DECREF(PyBtn);
			return NULL;
		}
		Py_DECREF(PyBtn);
	}

	if (state.rpt_mode & CWIID_RPT_ACC) {
		PyObject *PyAcc = Py_BuildValue("(B,B,B)",
							            state.acc[CWIID_X],
	                                    state.acc[CWIID_Y],
	                                    state.acc[CWIID_Z]);
		if (!PyAcc) {
			Py_DECREF(PyState);
			return NULL;
		}
		if (PyDict_SetItemString(PyState, "acc", PyAcc)) {
			Py_DECREF(PyState);
			Py_DECREF(PyAcc);
			return NULL;
		}
		Py_DECREF(PyAcc);
	}

	if (state.rpt_mode & CWIID_RPT_IR) {
		int i;
		PyObject *PyIr = PyList_New(CWIID_IR_SRC_COUNT);

		if (!PyIr) {
			Py_DECREF(PyState);
			return NULL;
		}

		if (PyDict_SetItemString(PyState, "ir_src", PyIr)) {
			Py_DECREF(PyState);
			Py_DECREF(PyIr);
			return NULL;
		}

		Py_DECREF(PyIr);

		for (i=0; i < CWIID_IR_SRC_COUNT; i++) {
			PyObject *PyIrSrc;
			PyObject *PySize;

			if (state.ir_src[i].valid) {
				PyIrSrc = Py_BuildValue("{s:(I,I)}",
				                        "pos",
				                          state.ir_src[i].pos[CWIID_X],
				                          state.ir_src[i].pos[CWIID_Y]);
				if (!PyIrSrc) {
					Py_DECREF(PyState);
					return NULL;
				}

				if (state.ir_src[i].size != -1) {
					if (!(PySize = PyInt_FromLong(
					  (long)state.ir_src[i].size))) {
						Py_DECREF(PyState);
						Py_DECREF(PyIrSrc);
						return NULL;
					}
					if (PyDict_SetItemString(PyIrSrc, "size", PySize)) {
						Py_DECREF(PyState);
						Py_DECREF(PyIrSrc);
						Py_DECREF(PySize);
						return NULL;
					}

					Py_DECREF(PySize);
				}
			}
			else {
				Py_INCREF(PyIrSrc = Py_None);
			}

			PyList_SET_ITEM(PyIr, i, PyIrSrc);
		}
	}

	switch (state.ext_type) {
		PyObject *PyExt;
	case CWIID_EXT_NUNCHUK:
		if (state.rpt_mode & CWIID_RPT_NUNCHUK) {
			PyExt = Py_BuildValue("{s:(B,B),s:(B,B,B),s:I}",
			                      "stick",
			                        state.ext.nunchuk.stick[CWIID_X],
			                        state.ext.nunchuk.stick[CWIID_Y],
			                      "acc",
			                        state.ext.nunchuk.acc[CWIID_X],
			                        state.ext.nunchuk.acc[CWIID_Y],
			                        state.ext.nunchuk.acc[CWIID_Z],
			                      "buttons", state.ext.nunchuk.buttons);

			if (!PyExt) {
				Py_DECREF(PyState);
				return NULL;
			}

			if (PyDict_SetItemString(PyState, "nunchuk", PyExt)) {
				Py_DECREF(PyState);
				Py_DECREF(PyExt);
				return NULL;
			}

			Py_DECREF(PyExt);
		}
		break;
	case CWIID_EXT_CLASSIC:
		if (state.rpt_mode & CWIID_RPT_CLASSIC) {
			PyExt = Py_BuildValue("{s:(B,B),s:(B,B),s:B,s:B,s:I}",
			                      "l_stick",
			                        state.ext.classic.l_stick[CWIID_X],
			                        state.ext.classic.l_stick[CWIID_Y],
			                      "r_stick",
			                        state.ext.classic.r_stick[CWIID_X],
			                        state.ext.classic.r_stick[CWIID_Y],
			                      "l", state.ext.classic.l,
			                      "r", state.ext.classic.r,
			                      "buttons", state.ext.classic.buttons);

			if (!PyExt) {
				Py_DECREF(PyState);
				return NULL;
			}

			if (PyDict_SetItemString(PyState, "classic", PyExt)) {
				Py_DECREF(PyState);
				Py_DECREF(PyExt);
				return NULL;
			}

			Py_DECREF(PyExt);
		}
		break;
	default:
		break;
	}

	return PyState;
}

static PyObject *Wiimote_get_acc_cal(Wiimote *self, PyObject *args,
                                     PyObject *kwds)
{
	static char *kwlist[] = { "ext_type", NULL };
	int ext_type;
	struct acc_cal acc_cal;
	PyObject *PyAccCal;

	if (!PyArg_ParseTupleAndKeywords(args, kwds,
	                                 "i:cwiid.Wiimote.get_acc_cal", kwlist,
	                                 &ext_type)) {
		return NULL;
	}

	if (cwiid_get_acc_cal(self->wiimote, ext_type, &acc_cal)) {
		PyErr_SetString(PyExc_IOError, "Wiimote get acc cal error");
		return NULL;
	}

	if (!(PyAccCal = Py_BuildValue("([i,i,i],[i,i,i])", acc_cal.zero[0],
	                               acc_cal.zero[1], acc_cal.zero[2],
	                               acc_cal.one[0], acc_cal.one[1],
	                               acc_cal.one[2]))) {
		return NULL;
	}

	return PyAccCal;
}

static PyObject *Wiimote_request_status(Wiimote *self)
{
	if (cwiid_request_status(self->wiimote)) {
		PyErr_SetString(PyExc_IOError, "Wiimote request status error");
		return NULL;
	}

	Py_RETURN_NONE;
}

static int Wiimote_set_led(Wiimote *self, PyObject *PyLed, void *closure)
{
	long led;

	if (((led = PyInt_AsLong(PyLed)) == -1) && PyErr_Occurred()) {
		return -1;
	}

	if (cwiid_set_led(self->wiimote, (uint8_t)led)) {
		PyErr_SetString(PyExc_IOError, "Wiimote set led error");
		return -1;
	}

	return 0;
}

static int
	Wiimote_set_rumble(Wiimote *self, PyObject *PyRumble, void *closure)
{
	long rumble;

	if (((rumble = PyInt_AsLong(PyRumble)) == -1) && PyErr_Occurred()) {
		return -1;
	}

	if (cwiid_set_rumble(self->wiimote, (uint8_t)rumble)) {
		PyErr_SetString(PyExc_IOError, "Wiimote set rumble error");
		return -1;
	}

	return 0;
}

static int
	Wiimote_set_rpt_mode(Wiimote *self, PyObject *PyRptMode, void *closure)
{
	long rpt_mode;

	if (((rpt_mode = PyInt_AsLong(PyRptMode)) == -1) && PyErr_Occurred()) {
		return -1;
	}

	if (cwiid_set_rpt_mode(self->wiimote, (uint8_t)rpt_mode)) {
		PyErr_SetString(PyExc_IOError, "Wiimote set rpt_mode error");
		return -1;
	}

	return 0;
}

/* static PyObject *Wiimote_command(Wiimote *self, PyObject *args, PyObject *kwds)
{
	static char *kwlist[] = { "command", "flags", NULL };
	int command, flags;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", kwlist, &command,
	                                 &flags)) {
		return NULL;
	}

	cwiid_command(self->wiimote, (enum cwiid_command)command, (uint8_t)flags);

	Py_RETURN_NONE;
}
*/

static PyObject *Wiimote_read(Wiimote *self, PyObject *args, PyObject *kwds)
{
	static char *kwlist[] = { "flags", "offset", "len", NULL };
	unsigned char flags;
	unsigned int offset;
	unsigned int len;
	void *buf;
	PyObject *pyRetBuf;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "BII:cwiid.Wiimote.read",
	                                 kwlist, &flags, &offset, &len)) {
		return NULL;
	}

	if (!(pyRetBuf = PyBuffer_New(len))) {
		return NULL;
	}
	if (PyObject_AsWriteBuffer(pyRetBuf, &buf, (int *)&len)) {
		Py_DECREF(pyRetBuf);
		return NULL;
	}
	if (cwiid_read(self->wiimote,flags,offset,len,buf)) {
		PyErr_SetString(PyExc_IOError, "Wiimote read error");
		Py_DECREF(pyRetBuf);
		return NULL;
	}

	return pyRetBuf;
}

static PyObject *Wiimote_write(Wiimote *self, PyObject *args, PyObject *kwds)
{
	static char *kwlist[] = { "flags", "offset", "buffer", NULL };
	unsigned char flags;
	unsigned int offset;
	void *buf;
	int len;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "BIt#:cwiid.Wiimote.write",
	                                 kwlist, &flags, &offset, &buf, &len)) {
		return NULL;
	}

	if (cwiid_write(self->wiimote, flags, offset, len, buf)) {
		PyErr_SetString(PyExc_IOError, "Wiimote write error");
		return NULL;
	}

	Py_RETURN_NONE;
}

static void
	CallbackBridge(cwiid_wiimote_t *wiimote, int mesg_count,
	               union cwiid_mesg mesg[], struct timespec *t)
{
	PyObject *ArgTuple;
	PyObject *PySelf;
	PyGILState_STATE gstate;

	gstate = PyGILState_Ensure();

	ArgTuple = ConvertMesgArray(mesg_count, mesg);

	/* Put id and the list of messages as the arguments to the callback */
	PySelf = (PyObject *) cwiid_get_data(wiimote);
	PyObject_CallFunction(((Wiimote *)PySelf)->callback, "(O)", ArgTuple);

	Py_XDECREF(ArgTuple);
	PyGILState_Release(gstate);
}

/* This is the function responsible for marshaling the cwiid messages from
 * C to python. It's rather complicated since it uses a complex C union
 * to store the data and multiple enumerations to figure out what data is
 * actually being sent. Neither of these common C types really translate
 * well into Python. I've done my best to translate it to python as follows:
 *
 * Python callback takes arg (mesgs). The mesgs is a list of
 * mesg tuples which contain the mesg type and a dict of the arguments.
 * 
 * Ex:
 * mesgs =>[(cwiid.STATUS_MESG,{"battery":battery,"ext_type":ext_type}),
 *          (cwiid.BTN_MESG,buttons), 
 *          (cwiid.ACC_MESG,(x,y,z)),
 *          (cwiid.IR_MESG,[{"pos":(x,y),"size":size}, ...]),
 *          (cwiid.NUNCHUK_MESG,{"stick":(x,y),"acc":(x,y,z),
 *                               "buttons":buttons},
 *          (cwiid.CLASSIC_MESG,{"l_stick":(x,y),"r_stick":(x,y),"l":l,"r":r,
 *                               "buttons":buttons},
 *          (cwiid.ERROR_MESG,error)]
 */
PyObject *ConvertMesgArray(int mesg_count, union cwiid_mesg mesg[])
{
	PyObject *mesglist; /* List of message tuples */
	PyObject *amesg; /* A single message (type, [arguments]) */
	PyObject *mesgVal; /* Dictionary of arguments for a message */
	PyObject *PyIrList;
	int i, j;

	if (!(mesglist = PyList_New(mesg_count))) {
		return NULL;
	}

	for (i = 0; i < mesg_count; i++) {
		switch (mesg[i].type) {
		case CWIID_MESG_STATUS:
			mesgVal = Py_BuildValue("{s:B,s:i}",
			                        "battery", mesg[i].status_mesg.battery,
			                        "ext_type", mesg[i].status_mesg.ext_type);
			break;
		case CWIID_MESG_BTN:
			mesgVal = Py_BuildValue("I", mesg[i].btn_mesg.buttons);
			break;
		case CWIID_MESG_ACC:
			mesgVal = Py_BuildValue("(B,B,B)", mesg[i].acc_mesg.acc[CWIID_X],
			                                   mesg[i].acc_mesg.acc[CWIID_Y],
			                                   mesg[i].acc_mesg.acc[CWIID_Z]);
			break;
		case CWIID_MESG_IR:
			mesgVal = NULL;

			if (!(PyIrList = PyList_New(CWIID_IR_SRC_COUNT))) {
				break;
			}

			for (j=0; j < CWIID_IR_SRC_COUNT; j++) {
				PyObject *PyIrSrc;
				PyObject *PySize;

				if (mesg[i].ir_mesg.src[j].valid) {
					PyIrSrc = Py_BuildValue("{s:(I,I)}",
					             "pos",
					               mesg[i].ir_mesg.src[j].pos[CWIID_X],
					               mesg[i].ir_mesg.src[j].pos[CWIID_Y]);

					if (!PyIrSrc) {
						Py_DECREF(PyIrList);
						PyIrList = NULL;
						break;
					}

					if (mesg[i].ir_mesg.src[j].size != -1) {
						if (!(PySize = PyInt_FromLong(
						  (long)mesg[i].ir_mesg.src[j].size))) {
							Py_DECREF(PyIrList);
							Py_DECREF(PyIrSrc);
							PyIrList = NULL;
							break;
						}
						if (PyDict_SetItemString(PyIrSrc, "size", PySize)) {
							Py_DECREF(PyIrList);
							Py_DECREF(PyIrSrc);
							Py_DECREF(PySize);
							PyIrList = NULL;
							break;
						}

						Py_DECREF(PySize);
					}
				}
				else {
					Py_INCREF(PyIrSrc = Py_None);
				}
				PyList_SET_ITEM(PyIrList, j, PyIrSrc);
			}

			if (!PyIrList) {
				break;
			}

			mesgVal = PyIrList;
			break;
		case CWIID_MESG_NUNCHUK:
			mesgVal = Py_BuildValue("{s:(B,B),s:(B,B,B),s:I}",
			                        "stick",
			                          mesg[i].nunchuk_mesg.stick[CWIID_X],
			                          mesg[i].nunchuk_mesg.stick[CWIID_Y],
			                        "acc",
			                          mesg[i].nunchuk_mesg.acc[CWIID_X],
			                          mesg[i].nunchuk_mesg.acc[CWIID_Y],
			                          mesg[i].nunchuk_mesg.acc[CWIID_Z],
			                        "buttons", mesg[i].nunchuk_mesg.buttons);
			break;
		case CWIID_MESG_CLASSIC:
			mesgVal = Py_BuildValue("{s:(B,B),s:(B,B),s:B,s:B,s:I}",
			             "l_stick",
			               mesg[i].classic_mesg.l_stick[CWIID_X],
			               mesg[i].classic_mesg.l_stick[CWIID_Y],
			             "r_stick",
			               mesg[i].classic_mesg.r_stick[CWIID_X],
			               mesg[i].classic_mesg.r_stick[CWIID_Y],
			             "l", mesg[i].classic_mesg.l,
			             "r", mesg[i].classic_mesg.r,
			             "buttons", mesg[i].classic_mesg.buttons);
			break;
		case CWIID_MESG_ERROR:
			mesgVal = Py_BuildValue("i", mesg[i].error_mesg.error);
			break;
		default:
			Py_INCREF(mesgVal = Py_None);
			break;
		}

		if (!mesgVal) {
			return NULL;
		}

		/* Finally Put the type next to the message in a tuple and
		 * append them to the list of messages */
		if (!(amesg = Py_BuildValue("(iO)", mesg[i].type, mesgVal))) {
			Py_DECREF(mesgVal);
			return NULL;
		}
		Py_DECREF(mesgVal);
		PyList_SET_ITEM(mesglist, i, amesg);
	}

	return mesglist;
}