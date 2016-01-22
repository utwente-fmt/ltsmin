LTSmin [![Build Status](https://travis-ci.org/utwente-fmt/ltsmin.svg?branch=master)](https://travis-ci.org/utwente-fmt/ltsmin)
===

Model Checking and Minimization of Labelled Transition Systems 
---

LTSmin started out as a generic toolset for manipulating labelled
transition systems. Meanwhile the toolset was extended to a
a full (LTL/CTL) model checker, while maintaining its language-independent
characteristics.

To obtain its input, LTSmin connects a sizeable number of existing
(verification) tools: *muCRL*, *mCRL2*, *DiVinE*, *SPIN* (*SpinS*), *UPPAAL* (*opaal*), *SCOOP*
and *CADP*. Moreover, it allows to reuse existing tools with new state space
generation techniques by exporting LTSs into various formats.

Implementing support for a new language module is in the
order of 200--600 lines of C "glue" code, and automatically yields
tools for standard reachability checking (e.g., for state space
generation and verification of safety properties), reachability with
symbolic state storage (vector set), fully symbolic (BDD-based)
reachability, distributed reachability (MPI-based), and multi-core
reachability (including multi-core compression and incremental
hashing).

The synergy effects in the LTSmin implementation are enabled by a
clean interface: all LTSmin modules work with a unifying state
representation of integer vectors of fixed size, and the PINS
dependency matrix which exploits the combinatorial nature of model
checking problems. This splits model checking tools into three mostly
independent parts: language modules, PINS optimizations, and model
checking algorithms.

On the other hand, the implementation of a verification algorithm
based on the PINS matrix automatically has access to muCRL, mCRL2,
DVE, PROMELA, SCOOP, UPPAAL xml and ETF language modules.

Finally, all tools benefit from PINS2PINS optimizations, like local
transition caching (which speeds up slow state space generators),
matrix regrouping (which can drastically reduce run-time and memory
consumption of symbolic algorithms), partial order reduction and
linear temporal logic.

Supported Systems
---

 - GNU/Linux (tested on Arch Linux, Ubuntu, Debian,
    OpenSuSE 11.2 and Red Hat Enterprise Linux 6)
 - MacOS X, version 10.10 "Yosemite"
 - MacOS X, version 10.7 "Lion"
 - MacOS X, version 10.6 "Snow Leopard" (no multi-core muCRL/mCRL2)
 - MacOS X, version 10.5 "Leopard" (no multi-core muCRL/mCRL2)
 - Cygwin/Windows (tested on Windows 7 with Cygwin 1.7)
 
For the use of the multi-core BDD package Sylvan and the multi-core
reachability algorithms (`*2lts-mc`), we further recommend using a 64-bit OS.

Installation Instructions
---

If you are building the software from a Git repository rather than a
release tarball, refer to Section "Building from a Git Repository" for
additional set-up instructions.  Then install the dependencies listed
in Section "Build Dependencies" below.

    # Unpack the tarball
    $ tar xvzf ltsmin-<version>.tar.gz
    $ cd ltsmin-<version>

    # Configure
    $ ./configure --disable-dependency-tracking --prefix /path/

It is a good idea to check the output of ./configure, to see whether
all dependencies were found.

    # Build
    $ make

    # Install
    $ make install
    
You can also run tests with

    # Run tests
    $ make check

### Additional Build Options

#### configure options

For one-shot builds, the following option speeds up the build process
by not recording dependencies:

    ./configure --disable-dependency-tracking ...

Non-standard compilers, etc., can be configured by using variables:

    ./configure CFLAGS='-O3 -m64' MPICC=/sw/openmpi/1.2.8/bin/mpicc ...

This would add some options to the standard CFLAGS settings used for
building, to enable more optimizations and force a 64-bit build (for
the GCC C compiler).  Furthermore, the MPI compiler wrapper is set
explicitly instead of searching it in the current shell PATH.

Note that libraries installed in non-standard places need special
attention: to be picked up by the configure script, library and header
search paths must be added, e.g.:

    ./configure LDFLAGS=-L/opt/local/lib CPPFLAGS=-I/opt/local/include

Additional setting of (DY)LD_LIBRARY_PATH might be needed for the
dynamic linker/loader (see, e.g., "man ld.so" or "man dyld").

See `./configure --help` for the list of available variables,
and file INSTALL for further details.

#### make targets

The following additional make targets are supported:

    mostlyclean::
    clean::
        Clean the source tree.

    doxygen-doc::
        Builds Doxygen documentation for the source code.


Build Dependencies
---

We list the external libraries and tools which are required to build
this software.

#### popt

Download popt (>= 1.7) from <http://rpm5.org/files/popt/>.  We tested
with popt 1.14.

#### zlib

Download zlib from <http://www.zlib.net/>.

#### GNU make

Download GNU make from <http://www.gnu.org/software/make/>.

#### Flex

Download Flex (>= 2.5.35) from <http://flex.sourceforge.net/>.  We
tested with flex 2.5.35.

#### Apache Ant

Download Apache Ant from <http://ant.apache.org/>.  We tested with ant
1.7.1.  Note that ant is not required for building from a distribution
tarball (unless Java files were modified).  Note that we require
JavaCC task support for Ant.

### Optional Dependencies

#### muCRL

Download muCRL (>= 2.18.5) from <http://www.cwi.nl/~mcrl/mutool.html>.
We tested with muCRL-2.18.5.  Without muCRL, the AtermDD decision
diagram package will not be built.

Note that for 64-bit builds, you have to explicitly configure muCRL
for this (otherwise, a faulty version is silently build):

    ./configure --with-64bit

For best performance, we advise to configure muCRL like this:

    ./configure CC='gcc -O2' --with-64bit

#### mCRL2

Download the latest version of mCRL2 from <http://www.mcrl2.org/>.

Build and install mCRL2:

      cmake . -DCMAKE_INSTALL_PREFIX=...
      make
      make install

The graphical tools of mCRL2 are not required for ltsmin to work,
hence you can also build mCRL2 without:

    cmake . -DMCRL2_ENABLE_GUI_TOOLS=OFF -DCMAKE_INSTALL_PREFIX=...

Note: to enable the PBES tools use the latest svn version of mCRL2.

#### CADP

See the CADP website <http://www.inrialpes.fr/vasy/cadp/> on how to
obtain a license and download the CADP toolkit.

#### TBB malloc

For multi-core reachability on timed automata with the opaal frontend, install 
TBB malloc. Scalability can be limited without a concurrent allocator, hence 
the opaal2lts-mc frontend is not compiled in absence of TBB malloc. 
See <http://threadingbuildingblocks.org/file.php?fid=77>

#### DiVinE 2

Download version 2.4 of DiVinE from
<http://divine.fi.muni.cz/>.  We tested with the version 2.4.  Apply
the patch from <contrib/divine-2.4.patch> to the DiVinE source tree:

    cd divine-2.4
    patch -p1 < /path/to/ltsmin/contrib/divine-2.4.patch

Alternatively, download a LTSmin-enabled version of DiVinE via git:

    git clone http://fmt.cs.utwente.nl/tools/scm/divine2.git

Build with:

    cd divine2
    mkdir _build && cd _build
    cmake .. -DGUI=OFF -DRX_PATH= -DCMAKE_INSTALL_PREFIX=... -DMURPHI=OFF
    make
    make install

On MacOS X, option -DHOARD=OFF might have to be added to the cmake command line to make it compile without errors. 
Also, on MacOS X Divine2 only compiles with the GNU std C++ library.
Thus on MacOS X one has to provide the option `-DCMAKE_CXX_FLAGS="-stdlib=libstdc++"`

The LTSmin configure script will find the DiVinE installation
automatically, if the divine binary is in the search path.

With suitable options, the `divine compile` DVE compiler produces
LTSmin compatible libraries:

    divine compile -l model.dve

This produces a file `model.dve2C`, which can also be passed to
LTSmin tools.  (This step is done automatically by the LTSmin tools
when passing in a `.dve` model, doing it manually is rarely needed.)


####Scoop

See the scoop/README file.

####SpinS / Promela

Use SpinS (distributed as submodule LTSmin) to compile PROMELA model
`leader.pm` to 'leader.pm.spins':

        spins -o3 leader.pm

The optional flag +-o3+ turns off control flow optimizations.

The resulting compiled SpinS module can be loaded by  all SpinS-related LTSmin
tools (prom*).

#### opaal / UPPAAL XML

Download opaal from <https://code.launchpad.net/~opaal-developers/opaal/opaal-ltsmin-succgen>. Follow the included installation instructions.

UPPAAL xml models can be compiled to PINS binaries (.xml.so) using the included
opaal_ltsmin script.

    opaal_ltsmin --only-compile model.xml

The multcore LTSmin tool can explore the model (opaal2lts-mc). Provide the
TBB allocator location at load run time for good performance:
`LD_PRELOAD=/usr/lib/libjemalloc.so.1 opaal2lts-mc <model.xml.so>`


#### libDDD

Download libDDD (>= 1.7) from <http://move.lip6.fr/software/DDD/>.
We tested with libDDD 1.7.

#### MPI

In principle, any MPI library which supports MPI-IO should work.
However, we tested only with Open MPI <http://www.open-mpi.org/>.
Without MPI, the distributed tools (`xxx2lts-mpi`, `ltsmin-mpi`) will
not be built.

#### AsciiDoc

Download AsciiDoc (>= 8.4.4) from <http://www.methods.co.nz/asciidoc/>.
We tested with asciidoc-8.4.4.  Without asciidoc, documentation cannot
be rebuilt.  For convenience, release tarballs are shipping with
pre-built man pages and HTML documentation.

#### xmlto

Download xmlto from <http://cyberelk.net/tim/software/xmlto/>.  We
tested with xmlto-0.0.18.  Without xmlto, man pages cannot be rebuilt.
Note that xmlto in turn requires docbook-xsl to be installed.  We
tested with docbook-xsl-1.76.1.

#### Doxygen

Download Doxygen from <http://www.doxygen.org/>.  We tested with
doxygen-1.5.5.  Without doxygen, internal source code documentation
cannot be generated.

#### MacOS X

For cross-compilation builds on MacOS X, the Apple Developer SDKs must
be installed.  They are available from Apple
<http://developer.apple.com/tools/download/>, or from the MacOS X
installation CDs.

Building from a Git Repository
---

Before building the software as described above, the following commands
have to be executed in the top-level source directory:

    $ git submodule update --init
    $ ./ltsminreconf

### Dependencies

Building from another source than the release tarball requires some
extra tools to be installed:

#### GNU automake

Download automake (>= 1.10) from
<http://www.gnu.org/software/automake/>. We tested with automake-1.10.

#### GNU autoconf

Download autoconf (>= 2.60) from
<http://www.gnu.org/software/autoconf/>. We tested with autoconf-2.68.

#### GNU libtool

Download libtool (>= 2.2.6) from
<http://www.gnu.org/software/libtool/>. We tested with libtool-2.4.

#### Apache Ant

See above.

Contact
---

 - For support/questions, email: <ltsmin-support@lists.utwente.nl>.
 - For bug reports and feature suggestions, visit: <https://github.com/utwente-fmt/ltsmin/issues>.
 - And subscribe to our mailing list: <ltsmin-users-subscribe-request@lists.utwente.nl>.
