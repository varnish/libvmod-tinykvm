..
.. NB:  This file is machine generated, DO NOT EDIT!
..
.. Edit vmod.vcc and run make instead
..

.. role:: ref(emphasis)

.. _vmod_accept(3):

===========
vmod_accept
===========

-----------
Accept VMOD
-----------

:Manual section: 3

SYNOPSIS
========

import accept [from "path"] ;


DESCRIPTION
===========

vmod_accept aims at ease Accept headers sanitization.

API
===

CONTENTS
========

* :ref:`obj_rule`
* :ref:`func_rule.add`
* :ref:`func_rule.filter`
* :ref:`func_rule.remove`

.. _obj_rule:

Object rule
===========


Create a rule object, setting the fallback string.

.. _func_rule.add:

VOID rule.add(STRING)
---------------------

Prototype
	VOID rule.add(STRING string)

Add ``string`` to the list of valid choices.

.. _func_rule.remove:

VOID rule.remove(STRING)
------------------------

Prototype
	VOID rule.remove(STRING string)

Remove ``string`` to the list of valid choices.

.. _func_rule.filter:

STRING rule.filter(STRING)
--------------------------

Prototype
	STRING rule.filter(STRING string)

Parse string and try to find a valid choice. The first one found is returned
(they are tested in the same order they were added), otherwise, the fallback
string is returned.

COPYRIGHT
=========

::

  Copyright (c) 2016 Guillaume Quintard
  
  Author: Guillaume Quintard <guillaume.quintard@gmail.com>
  
  (vmodtool requires this format.)
  

