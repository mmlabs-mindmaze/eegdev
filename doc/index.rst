Welcome to eegdev's documentation!
==================================
eegdev is a library that provides an interface for accessing various EEG (and
other biosignals) acquisition systems in a unified way. The interface has been
designed in order to be both flexible and efficient. The device specific part
is implemented by the means of plugins which makes adding new device backend
fairly easy even if the library does not support them yet officially.

The core library not only provides a unified and consistent interfaces to the
eegdev users but it also provides many functionalities to the device backends
(plugins) ranging from configuration to data casting and scaling making writing
new device backend an easy task.

.. toctree::
   :caption: API module list
   :titlesonly:
   :maxdepth: 2

   eegdev.rst


Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`

