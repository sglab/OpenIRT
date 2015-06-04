/*
===============================================================================

  FILE:  rangemodel.h
  
  CONTENTS:
      
  PROGRAMMERS:
  
    martin isenburg@cs.unc.edu
  
  COPYRIGHT:
  
    Copyright (C) 2003 Martin Isenburg (isenburg@cs.unc.edu)
    
    This software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  
  CHANGE HISTORY:
  
    28 June 2004 -- changed constant SEARCHSHIFT to variable searchshift
    28 June 2004 -- changed constant LG_TOTF to variable lg_totf
    28 June 2004 -- changed constant TARGETRESCALE to variable targetrescale
    25 June 2004 -- removed port.h by combining it with rangemodel.h
    14 January 2003 -- adapted from michael schindler's code before SIGGRAPH
  
===============================================================================
*/
#ifndef RANGEMODEL_H
#define RANGEMODEL_H

/* this header byte needs to change in case incompatible change happen */
#define HEADERBYTE 1

/* definitions for the rangeencoder and rangedecoder */
#define CODE_BITS 32
#define TOP_VALUE ((unsigned int)1 << (CODE_BITS-1))
#define SHIFT_BITS (CODE_BITS - 9)
#define EXTRA_BITS ((CODE_BITS-2) % 8 + 1)
#define BOTTOM_VALUE (TOP_VALUE >> 8)

/* hard-coded definitions for the rangemodels */
#define TBLSHIFT 7

class RangeModel
{
public:
/* initialisation of model                             */
/* n   number of symbols in that model                 */
/* init  array of int's to be used for initialisation (NULL ok) */
/* compress  set to 1 on compression, 0 on decompression */
/* targetrescale  desired rescaling interval, should be < 1<<(lg_totf+1) */
/* lg_totf  base2 log of total frequency count         */
  RangeModel(unsigned int n, unsigned int *init, int compress, int targetrescale=0x800, int lg_totf=14);

/* deletion of qsmodel                                 */
  ~RangeModel();

/* reinitialisation of qsmodel                         */
/* init  array to be used for initialisation (NULL ok) */

  void reset(unsigned int *init);

/* retrieval of estimated frequencies for a symbol     */
/* sym  symbol for which data is desired; must be <n   */
/* sy_f frequency of that symbol                       */
/* lt_f frequency of all smaller symbols together      */
/* the total frequency is 1<<lg_totf                   */

  void getfreq(unsigned int sym, unsigned int *sy_f, unsigned int *lt_f);

/* find out symbol for a given cumulative frequency    */
/* lt_f  cumulative frequency                          */

  unsigned int getsym(unsigned int lt_f);

/* update model                                        */
/* sym  symbol that occurred (must be <n from init)    */

  void update(unsigned int sym);

  int n;             /* number of symbols */

//private:

  void dorescale();

  int left;          /* number of symbols to next rescale */
  int nextleft;      /* number of symbols with other increment */
  int rescale;       /* current interval between rescales */
  int targetrescale; /* target interval between rescales */
  int incr;          /* increment per update */
  int lg_totf;
  int searchshift;
  unsigned short *cf;         /* array of cumulative frequencies */
  unsigned short *newf;       /* array for collecting ststistics */
  unsigned short *search;     /* structure for searching on decompression */
};

#endif
