## Purpose

python_locator is header-only library to locate Python interpreter. It read python_required_version.txt to find Python interpreter. It support three types of Python locations. It supports providing Python in System, Bundle directory and Zib-based Python Interpreters.  

## Key/Value Pairs

The python_required_version.txt file support the following key/values pairs. The version values is of format X.Y.Z. X is the main version, Y is the subversion, and Z is patch version.  

- PYTHON_DEFAULT_MIN_VERSION = the minimum version value supported by the library.
- PYTHON_MIN_VERSION = the minimum version value supported by the application. It can be greater than or rqual to PYTHON_DEFAULT_MIN_VERSION.
- PYTHON_MAX_VERSION = the maximum version value supported by the application. 
- PYTHON_PREFERRED_VERSION = specify specific version value to override any minimum/maximum version values. It cannot be less than PYTHON_DEFAULT_MIN_VERSION.
- PYTHON_STRICT_MODE = True or False value. True uses patch version to be considered in loctor calculation. False ptach is not conside so 3.14.1 is same as 3.14.2.