#ifndef SPINJA_GREYBOX_H
#define SPINJA_GREYBOX_H

/**
 * \file prom-greybox.h
 * PINS interface for promela successor generator.
 * @inproceedings{eemcs22042,
             month = {September},
              issn = {2075-2180},
            author = {F. I. {van der Berg} and A. W. {Laarman}},
         num_pages = {8},
            series = {Electronic Proceedings in Theoretical Computer Science},
            editor = {K. {Heljanko} and W. J. {Knottenbelt}},
           address = {London},
         publisher = {Open Publishing Association},
              note = {http://eprints.eemcs.utwente.nl/22042/},
          location = {London, UK},
       event_dates = {17 Sept. 2012},
         booktitle = {11th International Workshop on Parallel and Distributed Methods in verifiCation, PDMC 2012, London, UK},
             title = {{Extending LTSmin with Promela through SpinJa}},
              year = {2012}
 }
 *
 */

#include <popt.h>
#include <pins-lib/pins.h>

extern struct poptOption spinja_options[];

/**
Load a spinja model.
*/
extern void SpinJaloadDynamicLib(model_t model, const char *name);
extern void SpinJaloadGreyboxModel(model_t model,const char*name);
extern void SpinJacompileGreyboxModel(model_t model,const char*name);

#endif
