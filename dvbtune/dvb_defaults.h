/* dvb_defaults.h

   Idea provided by Tomi Ollila, implemented by Dave Chapman.

   Copyright (C) Dave Chapman 2002
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
   Or, point your browser to http://www.gnu.org/copyleft/gpl.html

*/

#ifndef _DVB_DEFAULTS_H
#define _DVB_DEFAULTS_H

/* DVB-S */

// With a diseqc system you may need different values per LNB.  I hope
// no-one ever asks for that :-)

#define SLOF (11700*1000UL)
#define LOF1 (9750*1000UL)
#define LOF2 (10600*1000UL)

/* DVB-T */

/* Either uncomment one of the following lines, or add it to your
   "make" command.  e.g.

   make FINLAND=1
*/

//#define UK
//#define FINLAND
//#define FINLAND2

/* Firstly, lets define some world-wide defaults */
#define BANDWIDTH_DEFAULT           BANDWIDTH_8_MHZ
#define CONSTELLATION_DEFAULT       QAM_64
#define HIERARCHY_DEFAULT           HIERARCHY_NONE
#define LP_CODERATE_DEFAULT         FEC_1_2   

/* DVB-T */

#ifdef UK

/* UNITED KINGDOM settings */
#define HP_CODERATE_DEFAULT         FEC_2_3
#define TRANSMISSION_MODE_DEFAULT   TRANSMISSION_MODE_2K
#define GUARD_INTERVAL_DEFAULT      GUARD_INTERVAL_1_32

#endif

#ifdef FINLAND

/* FINLAND settings 1 */
#define HP_CODERATE_DEFAULT         FEC_2_3
#define TRANSMISSION_MODE_DEFAULT   TRANSMISSION_MODE_8K
#define GUARD_INTERVAL_DEFAULT	    GUARD_INTERVAL_1_8

#endif

#ifdef FINLAND2

/* FINLAND settings 2 */
#define HP_CODERATE_DEFAULT         FEC_1_2
#define TRANSMISSION_MODE_DEFAULT   TRANSMISSION_MODE_2K
#define GUARD_INTERVAL_DEFAULT      GUARD_INTERVAL_1_8

#endif

#ifndef HP_CODERATE_DEFAULT

#warning No DVB-T country defined in dvb_defaults.h
#warning defaulting to UK
#warning Ignore this if using Satellite or Cable

/* Default to the UK */
#define HP_CODERATE_DEFAULT         FEC_2_3
#define TRANSMISSION_MODE_DEFAULT   TRANSMISSION_MODE_2K
#define GUARD_INTERVAL_DEFAULT      GUARD_INTERVAL_1_32

#endif


#endif 
