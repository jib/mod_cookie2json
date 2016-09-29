mod_cookie2json
===============

Return cookie values as json/jsonp callback

Building
--------

Make sure you have apxs2 and perl installed, which on Ubuntu
you can get by running:

```
  $ sudo apt-get install apache2-dev perl
```

From the checkout directory run:
```
  $ perl build.pl
```

This will build the module on your system and is sufficient for running the tests. If you want to build, install & enable the module on your system:

```
  $ sudo perl build.pl --install
```

Configuration
-------------

See the file 'DOCUMENTATION' in the same directory as this
README for all the extra features this module has compared to
mod_usertrack, as well as documentation on the configuration
directives supported.


Testing
-------

*** Note:*** for this will need Apache, NodeJS and Perl installed.

First, start the backend node based server. It serves
as an endpoint and shows you the received url & headers
for every call:

```
  $ ./test/run_backend.sh
```

Next, start a custom Apache server. This will have all
the modules needed and the endpoints for testing:

```
  $ ./test/run_httpd.sh
```

Then, run the test suite:

```
  $ perl test/01_all.t
```

If you are testing in a container environment, `run_tests_docker.sh` is provided as a wrapper to run all these steps (see below for a sample Dockerfile), for example:

```
  $ docker run -t -w /build -v $(pwd):/build <image_name> ./run_tests_docker.sh
```

Run it as follows to enable diagnostic/debug output:

```
  $ perl test/01_all.t --debug
```

There will be an error log available, and that will be
especially useful if you built the library with `--debug`:

```
  $ tail -F test/error.log
```

Building your own package
-------------------------

Make sure you have **dpkg-dev**, **cdbs** and **debhelper** installed, which on Ubuntu you can get by running:

```
$ sudo apt-get install dpkg-dev cdbs debhelper
```

Then build the package by first compiling the module, then running buildpackage:

```
$ perl build.pl
$ dpkg-buildpackage -d -b
```

Using Docker
------------
To install all the dependencies and set up `nvm`, see the sample Dockerfile for building on the Trusty release of Ubuntu in `contrib/ubuntu-trusty`
