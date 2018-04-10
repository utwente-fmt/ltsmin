LTSmin [![Build Status](https://travis-ci.org/utwente-fmt/ltsmin.svg?branch=master)](https://travis-ci.org/utwente-fmt/ltsmin) [![FMT](http://fmt.cs.utwente.nl/images/fmt-logo.png)](http://fmt.cs.utwente.nl/) [![UT](https://www.symbitron.eu/wp-content/uploads/2013/10/UT_Logo_2400_Black_EN1-300x58.png)](https://www.utwente.nl/)
===

## What is LTSmin

LTSmin started out as a generic toolset for manipulating labelled
transition systems. Meanwhile the toolset was extended to a
a full (LTL/CTL/μ-calculus) model checker, while maintaining its language-independent
characteristics.

To obtain its input, LTSmin connects a sizeable number of existing
(verification) tools: *muCRL*, *mCRL2*, *DiVinE*, *SPIN* (*SpinS*), *UPPAAL* (*opaal*), *SCOOP*, *PNML*, *ProB*
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
DVE, PROMELA, SCOOP, UPPAAL xml, PNML, ProB, and ETF language modules.

Finally, all tools benefit from PINS2PINS optimizations, like local
transition caching (which speeds up slow state space generators),
matrix regrouping (which can drastically reduce run-time and memory
consumption of symbolic algorithms), partial order reduction and
linear temporal logic.

### Algorithmic Backends

LTSmin offers different analysis algorithms covering four disciplines (algorithmic backends):

* Symbolic CTL/mu-calculus model checking using different BDD/MDD packages, including the parallel BDD package Sylvan,
* Multi-core LTL model checking using tree compression,
* Sequential LTL model checking, with partial-order reduction and optional BDD-based state storage, and
* Distributed LTS instantiation, export, and minimization modulo branching/strong bisimulation.

The PINS interface divides our model checking tools cleanly into the two These algorithms each have their own strength, depending on the input model's structure and verification problem. For models with combinatorial state structure, the symbolic tools can process **billions of states per second** using only few memory. Models that exhibit much concurrency can be reduced significantly using partial-order reduction. Models with more dependencies can be explored with LTSmin's multi-core algorithms, which employ aggressive lossless state compression using a concurrent tree data structure. Finally, our distributed minimization techniques can aid the verification of multiple properties on a single state space.

### Language Front-ends

LTSmin supports language independence via its definition of a **Partitioned Next-State Interface** (PINS), which exposes enough internal structure of the input model to enable the highly effective algorithms above, while at the same time making it easy to connect your own language module. The interface is also simple enough to support very fast next-state functions such as SPIN's (performance comparison here). LTSmin already connects a sizeable number of existing verification tools as language modules, enabling the use of their modeling formalisms:

* [muCRL's](http://homepages.cwi.nl/~mcrl/) process algebra,
* [mCRL2's](http://www.mcrl2.org/) process algebra,
* [DiVinE's](http://divine.fi.muni.cz/) [DVE](http://divine.fi.muni.cz/manual.html#the-dve-specification-language) language, based on extended state machines,
* the builtin symbolic [ETF](assets/man/etf.html) format,
* [SPIN's Promela](http://spinroot.com/) via the included [SpinS](http://eprints.eemcs.utwente.nl/22042/), a [SpinJa](http://code.google.com/p/spinja/) spinoff,
* [UPPAAL's](http://www.uppaal.org/) timed automata via [opaal](http://opaal-modelchecker.com/),
* [SCOOP's](http://wwwhome.cs.utwente.nl/~timmer/scoop/) process algebra for Markov Automata,
* Ordinary place/transition Petri nets in [PNML](http://www.pnml.org/) format,
* B, Event-B, TLA+, and Z through [ProB](http://www3.hhu.de/stups/prob/index.php/The_ProB_Animator_and_Model_Checker), and
* [POSIX'/UNIX'](http://tldp.org/HOWTO/Program-Library-HOWTO/dl-libraries.html) dlopen API.

Moreover, LTSmin supports multiple export formats for state spaces, which allows interoperability with other tools like [CADP](http://www.inrialpes.fr/vasy/cadp/).

### the PINS Interface

The **Partitioned Next-State Interface** (PINS) splits up the next-state function in different groups. For example, each transition group can represent a line of code in an imperative language module or a summand in a process-algebraic language module. Using the static dependency information between transition groups and state vector variables (most groups only depend on a few variables), LTSmin's algorithms can exploit the combinatorial structure of the state space. This leads to exponential gains in performance for the symbolic algorithms, which can now learn the transition relation on-the-fly in a very effectively way, because it is partitioned. Using the same principle, LTSmin provides transition storing for transition caching with negligible memory overhead.

To connect a new language module, one merely needs to implement the PINS next-state functions and provide some type information on the state vector contents, which should be encoded according to PINS unifying integer vector format. By providing additional transition/state dependency information via the PINS [dependency matrices](http://eprints.eemcs.utwente.nl/15703/), the symbolic exploration algorithms and PINS2PINS modules (see below) can exploit their full potential. Finally, by providing few additional information on transition guards the partial order reduction algorithms become enabled.

### PINS2PINS Wrappers

The PINS interface divides our model checking tools cleanly into the two independent parts discussed above: language modules and model checking algorithms. However it also enables us to create PINS2PINS modules, that reside between the language module and the algorithm, and modify or optimize the next-state function. These PINS2PINS modules can benefit all algorithmic backends and can be turned on and off on demand:

* transition storing/caching speeds up slow language modules,
* [regrouping](assets/man/dve2lts-sym.html#_pins_options) speeds up the symbolic algorithms by optimizing dependencies, and
* [partial order reduction](http://essay.utwente.nl/61036/) reduces the state space by dropping irrelevant transitions.

![PINS-interface](assets/img/pins_modern.png)

Furthermore, we implement linear temporal logic (LTL) as a PINS2PINS module, which is automatically turned on when an LTL formula is supplied and transforms the state space on-the-fly by calculating the cross product with the formula.

## References

If you were to refer to the LTSmin toolset in your academic paper, we would appreciate if you would use one of the following references (depending on the part(s) of the toolset that you are referring to):

* Gijs Kant, Alfons Laarman, Jeroen Meijer, Jaco van de Pol, Stefan Blom and Tom van Dijk: LTSmin: High-Performance Language-Independent Model Checking. TACAS 2015
* Alfons Laarman, Jaco van de Pol and Michael Weber. Multi-Core LTSmin: Marrying Modularity and Scalability. NFM 2011
* Stefan Blom, Jaco van de Pol and Michael Weber. LTSmin: Distributed and Symbolic Reachability. CAV 2010

## Contributors

Ordered by the number of commits (January 2017) LTSmin's contributors are:

* [Alfons Laarman](http://alfons.laarman.com/),
* [Michael Weber](http://foldr.org/mw/),
* [Jeroen Meijer](http://jmeijer.nl/),
* [Stefan Blom](http://fmt.cs.utwente.nl/~sccblom/),
* [Jeroen Ketema](http://www.ketema.eu/),
* [Tom van Dijk](http://www.tvandijk.nl/),
* Elwin Pater,
* [Gijs Kant](http://fmt.cs.utwente.nl/~kant/),
* Vincent Bloemen
* [Jaco van de Pol](http://fmt.cs.utwente.nl/~vdpol/),
* [Philipp Körner](http://www.cs.hhu.de/en/research-groups/software-engineering-and-programming-languages/our-team/team/philipp-koerner.html)
* [Mads Chr. Olesen](http://people.cs.aau.dk/~mchro/),
* [Freark van der Berg](http://fivanderberg.com/),
* Steven van der Vegt,
* [Andreas Dalsgaard](http://people.cs.aau.dk/~andrease/),
* [Jeroen Keiren](http://www.jeroenkeiren.nl/),
* [Tobias Uebbing](http://www.tobias-uebbing.de/),
* [Axel Belinfante](http://wwwhome.ewi.utwente.nl/~belinfan/),
* Sander van Schouwenburg,
* Tien-Loong Siaw and
* [Dennis Guck](http://wwwhome.ewi.utwente.nl/~guckd/)

## Selected Documentation

*   [ltsmin](assets/man/ltsmin.html) (LTSmin's main man page)
*   [lps2lts-sym](assets/man/lps2lts-sym.html) (BDD-based reachability with mCRL2 frontend)
*   [dve2lts-mc](assets/man/dve2lts-mc.html) (multi-core reachability with DiVinE 2 frontend)
*   [prom2lts-mc](assets/man/prom2lts-mc.html) (multi-core reachability with Promela SpinS frontend)
*   [lpo2lts-seq](assets/man/lpo2lts-seq.html) (sequential enumerative reachability with muCRL frontend)
*   [etf2lts-dist](assets/man/etf2lts-dist.html) (distributed reachability with ETF frontend)
*   [lps2torx](assets/man/lps2torx.html) (TorX testing tool connector with mCRL2 frontend)
*   [pbes2lts-sym](assets/man/pbes2lts-sym.html) (Symbolic reachability tool with PBES frontend: [example](http://wwwhome.cs.utwente.nl/~kant/git/))
*   [pins2lts-sym](assets/man/pins2lts-sym.html) (Symbolic reachability tool using the dlopen API: [tutorial](https://github.com/utwente-fmt/ltsmin-tacas2015/tree/master/sokoban))
*   [mapa2lts-sym](assets/man/mapa2lts-sym.html) (Symbolic reachability tool with MAPA frontend)
*   [pnml2lts-sym](assets/man/pnml2lts-sym.html) (Symbolic reachability tool with PNML frontend)
*   [prob2lts-sym](assets/man/prob2lts-sym.html) (Symbolic reachability tool with ProB frontend)

More manpages can be found at [here](assets/man/).

## Supported Systems

 - GNU/Linux (tested on Arch Linux, Ubuntu, Debian,
    OpenSuSE 11.2 and Red Hat Enterprise Linux 6)
 - MacOS X, version 10.10 "Yosemite"
 - MacOS X, version 10.7 "Lion"
 - MacOS X, version 10.6 "Snow Leopard" (no multi-core muCRL/mCRL2)
 - MacOS X, version 10.5 "Leopard" (no multi-core muCRL/mCRL2)
 - Cygwin/Windows (tested on Windows 7 with Cygwin 1.7)
 
For the use of the multi-core BDD package Sylvan and the multi-core
reachability algorithms (`*2lts-mc`), we further recommend using a 64-bit OS.

## Installation Instructions

If you are building the software from a Git repository rather than a
release tarball, refer to Section [Building from a Git Repository](#building-from-a-git-repository) for
additional set-up instructions.  Then install the dependencies listed
in Section [Build Dependencies](#build-dependencies) below.

    # Unpack the tarball
    $ tar xvzf ltsmin-<version>.tar.gz
    $ cd ltsmin-<version>

    # Configure
    $ ./configure --disable-dependency-tracking --prefix /path/

### Some notes on configuring pkgconf

If you have installed `pkgconf` in a non-standard location, you may
need to specify the location of the pkg-config utility, e.g.:

    $ ./configure --disable-dependency-tracking --prefix /path/ PKG_CONFIG="/path/to/pkg-config"

If you have installed dependencies (e.g. Sylvan) in a non-standard location,
you may need to tell `pkgconf` to search for `*.pc` files in this non standard
location. E.g. if Sylvan is installed in `/opt/local/` and Sylvan's `sylvan.pc`
is located in `/opt/local/lib/pkgconfig`, 
you need to set `PKG_CONFIG_PATH` to `/opt/local/lib/pkgconfig`:

    $ ./configure --disable-dependency-tracking --prefix /path/ PKG_CONFIG_PATH="/opt/local/lib/pkgconfig"

Note that as usual, you can separate multiple paths to `*.pc` files with `:`.

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

##### Static Linking

If you want to configure LTSmin to statically link binaries,
LTSmin needs to run `pkg-config` with the `--static` flag.
This will resolve additional flags required for static linking, e.g.:

    $ ./configure --enable-pkgconf-static

#### make targets

The following additional make targets are supported:

    mostlyclean::
    clean::
        Clean the source tree.

    doxygen-doc::
        Builds Doxygen documentation for the source code.


## Build Dependencies

We list the external libraries and tools which are required to build
this software.

#### popt

Download popt (>= 1.7) from <http://rpm5.org/files/popt/>.  We tested
with popt 1.14.

#### zlib

Download zlib from <http://www.zlib.net/>.

#### GNU make

Download GNU make from <http://www.gnu.org/software/make/>.

#### Apache Ant

Download Apache Ant from <http://ant.apache.org/>.  We tested with ant
1.7.1.  Note that ant is not required for building from a distribution
tarball (unless Java files were modified).  Note that we require
JavaCC task support for Ant.

#### pkgconf

Download pkgconf from <https://github.com/pkgconf/pkgconf>.

### Optional Dependencies

#### Sylvan

To build the parallel symbolic algorithms, [Sylvan](https://github.com/trolando/sylvan)
(>=1.4) is required.
If Sylvan is installed in a non-standard location please refer to
[this note on aclocal and pkgconf](#a-note-on-aclocal-and-pkgconf),
and [this note on configuring pkgconf](#some-notes-on-configuring-pkgconf).

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


#### Scoop

See the scoop/README file.

#### SpinS / Promela

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

#### CZMQ

To build the ProB front-end, [CZMQ](http://czmq.zeromq.org/) is required.

#### libxml2

To build the Petri net front-end, [libxml2](http://xmlsoft.org/) is required.
Note that the libxml2 package that macOS provides is not properly configured.
If you want to build the PNML front-end, please install a proper libxml2
package, e.g. libxml2 in Homebrew. And do not forget to set `PKG_CONFIG_PATH`,
as suggested by Homebrew.

#### Spot

To use Büchi automata created by Spot, download Spot (>=2.3.1) 
from <https://spot.lrde.epita.fr/index.html>.

#### BuDDy

To use BuDDy's FDDs implementation for symbolic state storage, you have two
options.

1. Install [Spot](#Spot) which distributes a BuDDy fork called BDDX
    (recommended),
1. Install BuDDy from <https://github.com/utwente-fmt/buddy/releases>, which
    is a fork originally included in LTSmin.

If both versions of BuDDy are installed, LTSmin will use the Spot version
of BuDDy.

## Building from a Git Repository

Before building the software as described above, the following commands
have to be executed in the top-level source directory:

    $ git submodule update --init
    $ ./ltsminreconf

### A note on aclocal and pkgconf

If you have installed `pkgconf` in a non-standard location you may need
to specify the location to `pkg.m4` using the `ACLOCAL_PATH`
variable manually, before running `ltsminreconf`. Assuming
the path to `pkg.m4` is `~/.local/share/aclocal/pkg.m4`, run:

    $ ACLOCAL_PATH=~/.local/share/aclocal ./ltsminreconf

### Dependencies

Building from another source than the release tarball requires some
extra tools to be installed:

#### GNU automake

Download automake (>= 1.14) from
<http://www.gnu.org/software/automake/>. We tested with automake-1.14.

#### GNU autoconf

Download autoconf (>= 2.65) from
<http://www.gnu.org/software/autoconf/>. We tested with autoconf-2.69.

#### GNU libtool

Download libtool (>= 2.2.6) from
<http://www.gnu.org/software/libtool/>. We tested with libtool-2.4.

#### Flex

Download Flex (>= 2.5.35) from <http://flex.sourceforge.net/>.  We
tested with flex 2.5.35.

#### Bison

Download Bison from (>= 3.0.2) from <https://www.gnu.org/software/bison/>.

#### pkgconf

See above.

## Contact

 - For support/questions, email: <ltsmin-support@lists.utwente.nl>.
 - For bug reports and feature suggestions, visit: <https://github.com/utwente-fmt/ltsmin/issues>.
 - And subscribe to our mailing list: <ltsmin-users-subscribe-request@lists.utwente.nl>.

## Related papers
*   [Jeroen Meijer](http://jmeijer.nl), and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) _[Bandwidth and Wavefront Reduction for Static Variable Ordering in Symbolic Reachability Analysis](http://eprints.eemcs.utwente.nl/27067/01/main.pdf)_ . NFM 2016
*   [Jens Bendisposto](http://www.cs.hhu.de/lehrstuehle-und-arbeitsgruppen/softwaretechnik-und-programmiersprachen/unser-team/team/bendisposto.html), [Philipp Körner](http://www.cs.hhu.de/en/research-groups/software-engineering-and-programming-languages/our-team/team/philipp-koerner.html), [Michael Leuschel](http://www.cs.hhu.de/lehrstuehle-und-arbeitsgruppen/softwaretechnik-und-programmiersprachen/unser-team/team/leuschel.html), [Jeroen Meijer](http://jmeijer.nl), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/), [Helen Treharne](http://www.surrey.ac.uk/cs/people/helen_treharne/), and [Jorden Whitefield](http://www.surrey.ac.uk/cs/people/jorden_whitefield/) _[Symbolic Reachability Analysis of B through ProB and LTSmin](http://eprints.eemcs.utwente.nl/27071/01/main.pdf)_  . iFM 2016
*   [Tom van Dijk](http://wwwhome.cs.utwente.nl/dijkt/) and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) _[Sylvan: Multi-core Decision Diagrams](http://www.tvandijk.nl/wp-content/uploads/2015/01/sylvan_tacas15.pdf)_ . TACAS 2015
*   [Gijs Kant](http://wwwhome.cs.utwente.nl/kant/), [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/), [Jeroen Meijer](http://jmeijer.nl), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/), [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom/) and [Tom van Dijk](http://wwwhome.cs.utwente.nl/dijkt/) _[LTSmin: High-Performance Language-Independent Model Checking](http://wwwhome.ewi.utwente.nl/~meijerjjg/LTSmin_High-performance_Language-Independent_Model_Checking.pdf)_ . TACAS 2015
*   [Jeroen Meijer](http://jmeijer.nl), [Gijs Kant](http://wwwhome.cs.utwente.nl/kant/), [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom/) and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) _[Read, Write and Copy Dependencies for Symbolic Model Checking](http://dx.doi.org/10.1007/978-3-319-13338-6_16)_ . HVC 2014
*   [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/) and [Anton Wijs](http://www.win.tue.nl/~awijs/) _[Partial-Order Reduction for Multi-Core LTL Model Checking](http://dx.doi.org/10.1007/978-3-319-13338-6_20)_ . HVC 2014
*   [Tom van Dijk](http://wwwhome.cs.utwente.nl/dijkt/) and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) _[Lace: Non-blocking Split Deque for Work-Stealing](http://dx.doi.org/10.1007/978-3-319-14313-2_18)_ . MuCoCoS 2014
*   [Gijs Kant](http://wwwhome.cs.utwente.nl/kant/) and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) _[Generating and Solving Symbolic Parity Games](http://dx.doi.org/10.4204/EPTCS.159.2)_ . GRAPHITE 2014
*   [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/), Elwin Pater, [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) and [Michael Weber](http://wwwhome.cs.utwente.nl/michaelw/). _[Guard-based Partial-Order Reduction](http://eprints.eemcs.utwente.nl/23303/)_ . SPIN 2013
*   [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/), [Mads Chr. Olesen](http://people.cs.aau.dk/~mchro/) and [Andreas Dalsgaard](http://people.cs.aau.dk/~andrease/), [Kim G. Larsen](http://people.cs.aau.dk/~kgl/), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/). _[Multi-Core Emptiness Checking of Timed Büchi Automata using Inclusion Abstraction](http://eprints.eemcs.utwente.nl/23158/) _. CAV 2013
*   [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/) and [David Farago](http://lfm.iti.uni-karlsruhe.de/farago.php). _[Improved On-The-Fly Livelock Detection: Combining Partial Order Reduction and Parallelism for DFSFIFO](http://eprints.eemcs.utwente.nl/23159/)_ . NASA FM 2013
*   [Tom van Dijk](http://wwwhome.cs.utwente.nl/dijkt/), [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/) and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/). _[Multi-core and/or Symbolic Model Checking](http://eprints.eemcs.utwente.nl/22550/)_. AVOCS 2012
*   [Freark van der Berg](http://fivanderberg.com/) and [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/). _[SpinS: Extending LTSmin with Promela through SpinJa](http://eprints.eemcs.utwente.nl/22042/)_. PDMC 2012
*   [Tom van Dijk](http://wwwhome.cs.utwente.nl/dijkt/), [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/) and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/). _[Multi-core BDD Operations for Symbolic Reachability](http://eprints.eemcs.utwente.nl/22166/)_. PDMC 2012
*   [Tom van Dijk](http://wwwhome.cs.utwente.nl/dijkt/). _[The parallelization of binary decision diagram operations for model checking](http://essay.utwente.nl/61650/)_. 2012\. Thesis
*   [Sami Evangelista](http://www-lipn.univ-paris13.fr/~evangelista/), [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/), [Laure Petrucci](http://www-lipn.univ-paris13.fr/~petrucci/) and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/). _[Improved Multi-Core Nested Depth-First Search](http://eprints.eemcs.utwente.nl/21967/)_. ATVA 2012
*   [Andreas Dalsgaard](http://people.cs.aau.dk/~andrease/), [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/), [Kim G. Larsen](http://people.cs.aau.dk/~kgl/), [Mads Chr. Olesen](http://people.cs.aau.dk/~mchro/) and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/). _[Multi-Core Reachability for Timed Automata](http://eprints.eemcs.utwente.nl/21972/)_. FORMATS 2012
*   [Gijs Kant](http://wwwhome.cs.utwente.nl/kant/) and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/). _[Efficient Instantiation of Parameterised Boolean Equation Systems to Parity Games](http://eprints.eemcs.utwente.nl/22278/)_. Graphite 2012
*   [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/) and [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/). _[Variations on Multi-Core Nested Depth-First Search](http://eprints.eemcs.utwente.nl/20618/)_. PDMC 2011
*   <a href="">Elwin Pater</a>. _[Partial Order Reduction for PINS](http://essay.utwente.nl/61036/)_. 2011\. Thesis
*   <a href="">Tien Loong Siaw</a>. _[Saturation for LTSmin](http://essay.utwente.nl/61453/)_. 2012\. Thesis
*   [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/), [Rom Langerak](http://wwwhome.cs.utwente.nl/langerak/), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/), [Michael Weber](http://wwwhome.cs.utwente.nl/michaelw/) and [Anton Wijs](http://www.win.tue.nl/~awijs/). _[Multi-Core Nested Depth-First Search](http://eprints.eemcs.utwente.nl/20337/)_. ATVA 2011
*   [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) and [Michael Weber](http://wwwhome.cs.utwente.nl/michaelw/). _[Multi-Core LTSmin: Marrying Modularity and Scalability](http://eprints.eemcs.utwente.nl/20004/)_ . NFM 2011
*   [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) and [Michael Weber](http://wwwhome.cs.utwente.nl/michaelw/). _[Parallel Recursive State Compression for Free](http://eprints.eemcs.utwente.nl/20146/)_ . SPIN 2011
*   [Alfons Laarman](http://wwwhome.cs.utwente.nl/laarman/), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) and [Michael Weber](http://wwwhome.cs.utwente.nl/michaelw/). _[Boosting Multi-Core Reachability Performance with Shared Hash Tables](http://eprints.eemcs.utwente.nl/18437/)_. FMCAD 2010
*   [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom/), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) and [Michael Weber](http://wwwhome.cs.utwente.nl/michaelw/). _[LTSmin: Distributed and Symbolic Reachability](http://eprints.eemcs.utwente.nl/18152/)_. CAV 2010, LNCS 6174, pp. 354–359.
*   [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom/), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/) and [Michael Weber](http://wwwhome.cs.utwente.nl/michaelw/). _[Bridging the Gap between Enumerative and Symbolic Model Checkers](http://eprints.eemcs.utwente.nl/15703/)_, Technical Report TR-CTIT-09-30, CTIT, University of Twente, Enschede. (2009)
*   [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom/), [Bert Lisser](http://homepages.cwi.nl/~bertl/), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/), and [Michael Weber](http://wwwhome.cs.utwente.nl/michaelw/). _[A Database Approach to Distributed State Space Generation](http://dx.doi.org/10.1093/logcom/exp004)_. J Logic Computation  (2009)
*   [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom/), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/): _[Symbolic Reachability for Process Algebras with Recursive Data Types](http://dx.doi.org/10.1007/978-3-540-85762-4_6)_. ICTAC 2008: pp. 81–95
*   [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom/), [Bert Lisser](http://homepages.cwi.nl/~bertl/), [Jaco van de Pol](http://wwwhome.cs.utwente.nl/vdpol/), and [Michael Weber](http://wwwhome.cs.utwente.nl/michaelw/). _[A Database Approach to Distributed State Space Generation](http://dx.doi.org/10.1016/j.entcs.2007.10.018)_. ENTCS 198(1): pp. 17–32 (2007)
*   [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom), [Simona Orzan](http://www.win.tue.nl/~sorzan/): _[Distributed state space minimization](http://dx.doi.org/10.1007/s10009-004-0185-2)_. STTT 7(3): pp. 280–291 (2005)
*   [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom), [Simona Orzan](http://www.win.tue.nl/~sorzan/): _[A distributed algorithm for strong bisimulation reduction of state spaces](http://dx.doi.org/10.1007/s10009-004-0159-4)_. STTT 7(1): pp. 74–86 (2005)
*   [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom), Izak van Langevelde, [Bert Lisser](http://homepages.cwi.nl/~bertl/): _[Compressed and Distributed File Formats for Labeled Transition Systems](http://dx.doi.org/10.1016/S1571-0661(05)80097-0)_. ENTCS 89(1): (2003)
*   [Stefan Blom](http://wwwhome.cs.utwente.nl/sccblom), [Simona Orzan](http://www.win.tue.nl/~sorzan/): _[Distributed Branching Bisimulation Reduction of State Spaces](http://dx.doi.org/10.1016/S1571-0661(05)80099-4)_. ENTCS 89(1): (2003)

## License

Copyright (c) 2008 - 2018 Formal Methods and Tools, University of Twente
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

  * Neither the name of the University of Twente nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
