# Unreleased - Release 3.0 of the LTSmin toolset

- Improvements to the symbolic back-end (*2lts-sym):
    - Precise counting of sets using the bignum interface. Meaning you
      can now print the size of a set arbitrarily large without loss of
      precision.
    - Add feature that allows counting the size of a set using long doubles
      (in addition to doubles). The symbolic back-end will automatically 
      switch to long doubles when necessary.
    - Add option --next-union that computes the application of a transition
      relation together with the identity.
    - The symbolic back-end is now capable of invariant checking.
    - Multiple formulas, invariants are now supported through their respective
      commandline options. If LACE is used, these properties will be checked
      in parallel.
    - It is now possible to output all Sylvan statistics, such as the size
      of the node table, number of GCs etc.
    - Add option --mu-opt that enables a faster mu-calculus model checker.

- Improvements w.r.t. static variable ordering:
    - Invert the permutation of rows/columns (transition groups/state variables)
      using options --regroup=hf --regroup=vf respectively.
    - Transformations on the dependency matrix ignore read dependencies,
      since write dependencies seem to influence the actual size of Decision
      Diagrams to a much greater extent.
    - Add option --col-ins (see manpage) that can pick a column/state variable
      and insert it at a specific index.
    - A vast amount of static variable reordering algorithms have been added
      available through Boost and ViennaCL. Furthermore the FORCE algorithm and
      Noack's algorithms have been implemented, see [1]. In addition to these
      algorithms metrics related to the quality of the static variable order
      can be shown with option --regroup=mm.
    - A manual group/variable order can now be given with --row-perm/--col-perm
      respectively.

- Improvements to the LTSmin language (see `man ltsmin-pred`):
    - Added the enabledness operator `??` to the LTSMin language,
      see `man ltsmin-pred`.
    - Native LTSmin support for signed 32-bit integers is added,
      including some basic arithmatic operators.
    - Added a type format checker.

- A new Petri net front-end has been added to LTSmin, see [2]. The front-end
  reads Petri nets in the PNML format.

- A front-end for B, Event-B CSP, and Z is now available through ProB, see [3].

- The mCRL2 front-end now accepts files with .txt extension.

- Improvements to the PINS interface:
    - Add GBExit function that is called before a back-end exits.
    - Add functions GB<set|get>VarPerm and GB<set|get>GroupPerm that allows
      language front-ends to provide permutations. This is useful when
      front-ends have language specific algorithms for static variable reordering.

- Miscellaneous:
    - All default autoconf files such as README, NEWS are moved to *.md and
      sym-linked to their old file names. The markdown styling works better with
      Github.
    - Major refactor to the PINS regrouping layer, simplifying its use for
      developers.
    - LTSmin now uses Travis CI.

 1. Bandwidth and Wavefront Reduction for Static Variable Ordering in Symbolic Reachability Analysis -
     http://dx.doi.org/10.1007/978-3-319-40648-0_20
 1. Complete Results for the 2016 Edition of the Model Checking Contest -
     http://mcc.lip6.fr/
 1. Symbolic Reachability Analysis of B Through ProB and LTSmin -
     http://dx.doi.org/10.1007/978-3-319-33693-0_18

# January 23, 2015 - Release 2.1 of the LTSmin toolset

This release is accompanied by the tool paper:
LTSmin: High-Performance Language-Independent Model Checking [6].

- Improvements to symbolic backend (*2lts-sym):
    - Parallelized transition relatioBandwidth and Wavefront Reduction for Static Variable Ordering in Symbolic Reachability Analysisn learning and application using Lace [5] 
    - Exploiting read, write and copy dependencies [3]
    - Parallelized guard learning and application (--pins-guards, --no-soundness-check) [1]
    - Improved regrouping algorithm for bandwidth reduction (-rcw)
    - New parallel MDD implementation from the Sylvan package: LDDmc (--vset=lddmc) [5,7]
    - Support maximal progress for probabilistic LTSs

- New mu-calculus PINS layer (--mucalc):
    - Enables verification of mu-calculus properties 
      for any of the supported languages and all backends
    - A parity game is generated (instead of an LTS), 
      which can be solved with --pg-solve in the symbolic tool, 
      or with any parity game solver that supports the pgsolver format.

- Symbolic Parity game solver (--pg-solve, spgsolver) [4]:
    - Added parallel attractor computation (--attr=par)

- New frontend for MAPA: Scoop (mapa2lts{sym,dist})

- New frontend for dl-open (pins2*):
    - Example can be found in section 3.4 in [6].

- Improvements to the POR layer (PINS2PINS wrapper):
    - Added Valmari's weak POR dependencies (--weak)
    - Added deletion algorithm [1] (--por=del)
    - Added Valmari's SCC-based algorithm (--por=scc)

- Improvements to the multi-core backend (*2lts-mc):
    - Added CNDFS proviso for efficient POR in LTL (--proviso=cndfs) [2]
    - Refactored code and separated search algorithms in objects

- Improvements to distributed backend (*2lts-dist):
    - Added counter example tracing
    - Tau confluence detection
    - Support maximal progress for probabilistic LTSs

1. Guard-based Partial-Order Reduction (Extended Version) -
    http://dx.doi.org/10.1007/s10009-014-0363-9
1. Partial-Order Reduction for Multi-Core LTL Model Checking -
    http://dx.doi.org/10.1007/978-3-319-13338-6_20
1. Read, Write and Copy Dependencies for Symbolic Model Checking -
    http://dx.doi.org/10.1007/978-3-319-13338-6_16
1. Generating and Solving Symbolic Parity Games -
    http://dx.doi.org/10.4204/EPTCS.159.2
1. Lace: non-blocking split deque for work-stealing -
    http://dx.doi.org/10.1007/978-3-319-14313-2_18
1. LTSmin: High-Performance Language-Independent Model Checking -
    http://wwwhome.ewi.utwente.nl/~meijerjjg/LTSmin_High-performance_Language-Independent_Model_Checking.pdf
1. Sylvan: Multi-core Decision Diagrams -
    http://www.tvandijk.nl/wp-content/uploads/2015/01/sylvan_tacas15.pdf

# March 4, 2013 - Release 2.0 of the LTSmin toolset

- Refactored runtime and IO libraries, organized source and renamed tools:
    - *-reach       -> *2lts-sym  (Symbolic CTL/mu model checking, ETF output)
    - *2lts-mc      -- *2lts-mc   (Multi-core LTL model checking, compression)
    - *2lts-mpi     -> *2lts-dist (Distributed LTS generation, LTS output)
    - *2lts-gsea    -> *2lts-seq  (Sequential POR, LTL, LTS gen., BDD storage)
    - *2lts-grey    -- REMOVED    (Old sequential tool)
    - lts-compare   -- NEW        (Branching/Strong bisim. equivalence check)
    - ltsmin-tracepp-> ltsmin-printtrace    (Trace pretty printing)
    - ltsmin(-mpi)  -> ltsmin-reduce(-dist) (LTS minimization:LTSmin's origin)
    - ltstrans      -> lts-convert          (LTS file format conversion)
- New parallel BDD package: Sylvan [1,2]
    - BDD operations are parallelized using the Wool framework (included)
    - Use: *2lts-sym --vset=sylvan
           *2lts-seq --state=vset --vset=sylvan
- New frontend for UPPAAL xml timed automata via opaal [10] (only opaal2lts-mc).
   This adds semi-symbolic states and inclusion checks to PINS [3]:
    - Multi-core reachability on timed automata using inclusion abstraction
    - Supports all multi-core features, including NDFS with abstraction [4]
    - Added a strict BFS algorithm [3] to reduce abstracted state counts
      (--strategy=sbfs, default for opaal)
- New frontend for PBESs generating parity games for the symbolic solver [5]:
    - All backends: pbes2lts-sym, pbes2lts-mc, pbes2lts-dist, and pbes2lts-seq
    - Added a symbolic parity game solver: spgsolver
    - PGSolver file output format ('{ast}.pg')
- Extensions to the multi-core backend (*2lts-mc):
    - Support for muCRL/mCRL2/PBES using processes ({lpo,lps,pbes}2lts-mc)
    - Added CNDFS algorithm [6]
    - Added OWCTY algorithm [11] in the multi-core backends
    - Added DFS_FIFO algorithm for parallel livelock detection using POR [7]
    - Added strict BFS algorithms (--strategy=sbfs,pbfs, see [3,12])
    - Elementary support for writing LTSs (using pbfs) and POR (single core)
- Renamed SpinJa to SpinS [8], and added:
    - Valid end-state filter, assertions (--action=assert) and never claims
    - Guards and dependencies for POR
    - Support for Java 1.5 and VMs from different vendors (previously JDK 1.7)
- Optimized multi-core data structures (mc-lib):
    - Reduced initialization times by using calloc instead of malloc+memset
    - Reduced tree memory usage by 50% by splitting table for roots and leafs.
      Set the relative ratio using: --ratio=n (n=2 by default)
    - Parallel Cleary tree (--state=cleary-tree) halving memory usage [9,8]
    - Reduced memoized hash sizes to 16 bit
    - Thread-local allocation for better NUMA performance
    - New load balancer with simplified interface
- Added test suite, called via 'make check' (requires DejaGNU)

1. Multi-Core BDD Operations for Symbolic Reachability -
    http://eprints.eemcs.utwente.nl/22166/
1. Multi-core and/or Symbolic Model Checking -
    http://eprints.eemcs.utwente.nl/22550/
1. Multi-Core Reachability for Timed Automata -
    http://eprints.eemcs.utwente.nl/21972/
1. Multi-Core Emptiness Checking of Timed Buchi Automata using Inclusion
    Abstraction - http://eprints.eemcs.utwente.nl/23158
1. Efficient Instantiation of Parameterised Boolean Equations Systems to
    Parity Games - http://eprints.eemcs.utwente.nl/22278/
1. Improved Multi-Core Nested Depth First Search -
    http://eprints.eemcs.utwente.nl/21967/
1. Improved On-The-Fly Livelock Detection -
    http://eprints.eemcs.utwente.nl/23159
1. SpinS: Extending LTSmin with Promela via SpinJa -
    http://eprints.eemcs.utwente.nl/22042/
1. A Parallel Compact Hash Table - http://eprints.eemcs.utwente.nl/20648/
1. opaal:
    https://code.launchpad.net/~opaal-developers/opaal/opaal-ltsmin-succgen
1. J. Barnat et al. - A Time-Optimal On-the-Fly parallel algorithm for
        model checking of weak LTL properties
1. G.J. Holzmann - Parallelizing the Spin Model Checker

# February 22, 2012 - Release 1.8 of the LTSmin toolset

 - Implementation of Evangelista et al.'s parallel NDFS and variations, see:
   http://eprints.eemcs.utwente.nl/20618/ (Variations on Multi-Core NDFS)
 - Native implementations of Ciardo et al.'s saturation algorithms for
   AtermDD and ListDD (<spec>-reach --saturation=sat) (Tien-Loong Siaw)
 - Dependency matrix regrouping using simulated annealing (-rgsa, -rcsa)
 - SpinJa support for channel operations, run expressions with arguments,
   and preprocessor defines
 - New experimental I/O library and run-time environment
 - New tools employing the new I/O library and run-time environment:
   o lts-tracepp for pretty printing traces
   o sigmin-mpi for distributed reduction of labeled transition systems
   o sigmin-one for sequential reduction of labeled transition systems
   o ltstrans for conversion of labeled transition systems
   o ltscmp-one for sequential comparison of labeled transition systems
   o <spec>2lts-hre for distributed state space generation

# July 24, 2011 - Release 1.7.1 of the LTSmin toolset

 - Fix for Multi-Core NDFS algorithm: introduced wait counter
   (<spec>2lts-mc)
 - Introduced/improved color counters and option no-all-red
   (<spec>2lts-mc)

# June, 24, 2011 - Release 1.7 of the LTSmin toolset

 - New tools <spec>2lts-gsea
   (General State Exploring Algorithms)
 - PINS LTL layer
 - Several multi-core LTL search algorithms
   (<spec>2lts-mc)
 - Several sequential LTL search algorithms
   (<spec>2lts-grey, <spec>2lts-gsea)
 - PINS Partial-Order Reduction Layer
   (<spec>2lts-gsea)
 - Bare-bones symbolic model checker for state-based mu-calculus
 - New SpinJa frontend
 - Patched DiVinE 2.4 frontend required
 - Reimplementation of mcrl2 PINS frontend
 - Read/Write dependency matrices and combined printing
 - Vector set improvements and clean-up
 - Connection to the libDDD decision diagram package
 - Conversion from ETF to DVE (etf-convert)

# November 1, 2010 - Release 1.6 of the LTSmin toolset.

 - New frontend DVE2 (requires DiVinE 2.2)
 - Enumerative Multi-Core backend
   (<spec>2lts-mc)
 - Multi-Core TreeDBS compression for state vectors
   (<spec>2lts-mc)
 - Finding error actions symbolically (<spec>-reach --action)
 - Symbolic saturation-like strategies
 - Faster trace generation for <spec>-reach
 - BDD reordering for BuDDy vset

# December 1, 2009 - Release 1.5 of the LTSmin toolset.

 - New frontend DVE (requires DiVinE-cluster)
 - Bignum support for state counts in spec-reach tools
   (Jeroen Ketema)
 - spec-reach clean-up
 - 'tree' vector set implementation based on AtermDD

# September 17, 2009 - Release 1.4 of the LTSmin toolset.

 - New tool ce-mpi for distributed cycle elimination
   (Simona Orzan)
 - New tool ltsmin-tracepp for pretty-printing traces to
   deadlock states
 - TorX support factored out into separate tools
   {lpo,lps,nips}2torx
 - Enumerative DFS support
 - Enumerative deadlock detection and trace output
 - Reworked ETF support (non-backwards compatible)
 - bash completion for LTSmin tools (see contrib/)

# July 10, 2009 - Release 1.3 of the LTSmin toolset.

 - Regrouping optimizations of the PINS matrix
 - Connection to the CADP toolkit via pins_open
 - Tuning of the BDD usage
 - Significant performance improvements
 - Symbolic deadlock detection and trace output

# March 31, 2009 - Release 1.2 of the LTSmin toolset.

The main improvements in this release are:
 - Option parsing is now performed using the popt library. Thus, all
   long options now start with a double minus.
 - The user can choose between BuDDy and ATermDD as the decision diagram
   library used in the symbolic tools.
 - A new compressed file format for labeled transition systems with
   arbitrary numbers of state and edge labels has been implemented.
 - The symbolic tools can write the results of the reachability analysis
   in the form of an ETF (Enumerated Table Format) file. This ETF file
   can be used as input for the reachabilty tools and can be directly
   translated to DVE.


