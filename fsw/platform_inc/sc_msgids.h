/*************************************************************************
** File: sc_msgids.h 
**
**  Copyright � 2007-2014 United States Government as represented by the
**  Administrator of the National Aeronautics and Space Administration.
**  All Other Rights Reserved.
**
**  This software was created at NASA's Goddard Space Flight Center.
**  This software is governed by the NASA Open Source Agreement and may be
**  used, distributed and modified only pursuant to the terms of that
**  agreement.
**
** Purpose:
**   This file contains the message ID's used by Stored Command
**
** References:
**   Flight Software Branch C Coding Standard Version 1.2
**   CFS Development Standards Document
**
*************************************************************************/
#ifndef _sc_msgids_
#define _sc_msgids_

/*************************************************************************
 ** Macro Definitions
 *************************************************************************/

/**
 ** \name SC Command Message Numbers */
/** \{ */
#define SC_CMD_MID        (0x18A9) /**< \brief Msg ID for cmds to SC   */
#define SC_SEND_HK_MID    (0x18AA) /**< \brief Msg ID to request SC HK */
#define SC_1HZ_WAKEUP_MID (0x18AB) /**< \brief Msg ID to recieve the 1Hz */
/** \} */

/**
 ** \name SC Telemetery Message Number */
/** \{ */
#define SC_HK_TLM_MID (0x08AA) /**< \brief Msg ID to send telemtry down on */
/** \} */

#endif /*_sc_msgids_*/

/************************/
/*  End of File Comment */
/************************/
