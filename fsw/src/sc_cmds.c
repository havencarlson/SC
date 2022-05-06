/*************************************************************************
** File: sc_cmds.c 
**
**  Copyright � 2007-2015 United States Government as represented by the
**  Administrator of the National Aeronautics and Space Administration.
**  All Other Rights Reserved.
**
**  This software was created at NASA's Goddard Space Flight Center.
**  This software is governed by the NASA Open Source Agreement and may be
**  used, distributed and modified only pursuant to the terms of that
**  agreement.
**
** Purpose:
**   This file contains the functions to handle processing of ground
**   command requests, housekeeping requests, and table updates
**
** References:
**   Flight Software Branch C Coding Standard Version 1.2
**   CFS Development Standards Document
**
*************************************************************************/

/**************************************************************************
 **
 ** Include section
 **
 **************************************************************************/

#include "cfe.h"
#include "cfe_tbl_msg.h"
#include "sc_app.h"
#include "sc_cmds.h"
#include "sc_atsrq.h"
#include "sc_rtsrq.h"
#include "sc_loads.h"
#include "sc_utils.h"
#include "sc_state.h"
#include "sc_msgids.h"
#include "sc_events.h"
#include "sc_version.h"
#include "sc_rts.h"

/**************************************************************************
 **
 ** Functions
 **
 **************************************************************************/

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Processes a command from the ATS                                */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
void SC_ProcessAtpCmd(void)
{
    CFE_MSG_Message_t *     BufPtr;     /* ATS command pointer */
    SC_AtsEntryHeader_t *   Entry;      /* ATS entry pointer */
    int32                   EntryIndex; /* ATS entry location in table */
    uint8                   AtsIndex;   /* ATS selection index */
    uint32                  CmdIndex;   /* ATS command index */
    char                    TempAtsChar = ' ';
    int32                   Result;
    bool                    AbortATS = false;
    SC_AtsEntryHeaderBuf_t *EntryPtr;
    CFE_SB_MsgId_t          MessageID     = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t       CommandCode   = 0;
    bool                    ChecksumValid = 0;

    /*
     ** The following conditions must be met before the ATS command will be
     ** executed:
     ** 1.) The next time is <= the current time
     ** 2.) The next processor number = ATP
     ** 3.) The atp is currently EXECUTING
     */

    if ((!SC_CompareAbsTime(SC_AppData.NextCmdTime[SC_ATP], SC_AppData.CurrentTime)) &&
        (SC_AppData.NextProcNumber == SC_ATP) && (SC_OperData.AtsCtrlBlckAddr->AtpState == SC_EXECUTING))
    {
        /*
         ** Get a pointer to the next ats command
         */
        AtsIndex   = SC_ATS_NUM_TO_INDEX(SC_OperData.AtsCtrlBlckAddr->AtsNumber); /* remember 0..1 */
        CmdIndex   = SC_ATS_CMD_NUM_TO_INDEX(SC_OperData.AtsCtrlBlckAddr->CmdNumber);
        EntryIndex = SC_AppData.AtsCmdIndexBuffer[AtsIndex][CmdIndex];
        EntryPtr   = (SC_AtsEntryHeaderBuf_t *)&SC_OperData.AtsTblAddr[AtsIndex][EntryIndex];
        Entry      = &EntryPtr->Header;
        BufPtr     = (CFE_MSG_Message_t *)((uint8_t *)Entry + SC_ATS_HEADER_SIZE);

        /*
         ** Make sure the command has not been executed, skipped or has any other bad status
         */
        if (SC_OperData.AtsCmdStatusTblAddr[AtsIndex][CmdIndex] == SC_LOADED)
        {
            /*
             ** Make sure the command number matches what the command
             ** number is supposed to be
             */
            if (Entry->CmdNumber == (SC_ATS_CMD_INDEX_TO_NUM(CmdIndex)))
            {
                /*
                 ** Check the checksum on the command
                 **
                 */
                CFE_MSG_ValidateChecksum(BufPtr, &ChecksumValid);
                if (ChecksumValid == true)
                {
                    /*
                     ** Count the command for the rate limiter
                     */
                    SC_OperData.NumCmdsSec++;

                    /*
                     **  First check to see if the command is a switch command,
                     **  if it is, then execute the command now instead of sending
                     **  it out on the Software Bus (this is the only exception to
                     **  way stored commands are sent out).
                     **  A switch command located within an ATS is handled differently
                     **  than a switch command by the ground controller. If the switch
                     **  command is by the ground controller, SC waits until the next
                     **  second to prevent overlapping of two ATSs from duplicating
                     **  commands. If the Switch command is located within an ATS,
                     **  SC immediately executes the switch command.
                     */

                    CFE_MSG_GetMsgId(BufPtr, &MessageID);
                    CFE_MSG_GetFcnCode(BufPtr, &CommandCode);

                    if (MessageID == SC_CMD_MID && CommandCode == SC_SWITCH_ATS_CC)
                    {
                        /*
                         ** call the ground switch module
                         */
                        if (SC_InlineSwitch())
                        {
                            /*
                             ** Increment the counter and update the status for
                             ** this command
                             */
                            SC_OperData.AtsCmdStatusTblAddr[AtsIndex][CmdIndex] = SC_EXECUTED;
                            SC_OperData.HkPacket.AtsCmdCtr++;
                        }
                        else
                        { /* the switch failed for some reason */

                            SC_OperData.AtsCmdStatusTblAddr[AtsIndex][CmdIndex] = SC_FAILED_DISTRIB;
                            SC_OperData.HkPacket.AtsCmdErrCtr++;
                            SC_OperData.HkPacket.LastAtsErrSeq = SC_OperData.AtsCtrlBlckAddr->AtsNumber;
                            SC_OperData.HkPacket.LastAtsErrCmd = SC_OperData.AtsCtrlBlckAddr->CmdNumber;

                        } /* end if */
                    }
                    else
                    {
                        Result = CFE_SB_TransmitMsg(BufPtr, true);

                        if (Result == CFE_SUCCESS)
                        {
                            /* The command sent OK */
                            SC_OperData.AtsCmdStatusTblAddr[AtsIndex][CmdIndex] = SC_EXECUTED;
                            SC_OperData.HkPacket.AtsCmdCtr++;
                        }
                        else
                        { /* the command had Software Bus problems */
                            SC_OperData.AtsCmdStatusTblAddr[AtsIndex][CmdIndex] = SC_FAILED_DISTRIB;
                            SC_OperData.HkPacket.AtsCmdErrCtr++;
                            SC_OperData.HkPacket.LastAtsErrSeq = SC_OperData.AtsCtrlBlckAddr->AtsNumber;
                            SC_OperData.HkPacket.LastAtsErrCmd = SC_OperData.AtsCtrlBlckAddr->CmdNumber;

                            CFE_EVS_SendEvent(SC_ATS_DIST_ERR_EID, CFE_EVS_EventType_ERROR,
                                              "ATS Command Distribution Failed, Cmd Number: %d, SB returned: 0x%08X",
                                              Entry->CmdNumber, (unsigned int)Result);

                            if (SC_OperData.AtsCtrlBlckAddr->AtsNumber == SC_ATSA)
                                TempAtsChar = 'A';
                            else if (SC_OperData.AtsCtrlBlckAddr->AtsNumber == SC_ATSB)
                                TempAtsChar = 'B';

                            /* Mark this ATS for abortion */
                            AbortATS = true;
                        }
                    }
                }
                else
                { /* the checksum failed */
                    /*
                     ** Send an event message to report the invalid command status
                     */
                    CFE_EVS_SendEvent(SC_ATS_CHKSUM_ERR_EID, CFE_EVS_EventType_ERROR,
                                      "ATS Command Failed Checksum: Command #%d Skipped", Entry->CmdNumber);
                    /*
                     ** Increment the ATS error counter
                     */
                    SC_OperData.HkPacket.AtsCmdErrCtr++;

                    /*
                     ** Update the last ATS error information structure
                     */
                    SC_OperData.HkPacket.LastAtsErrSeq = SC_OperData.AtsCtrlBlckAddr->AtsNumber;
                    SC_OperData.HkPacket.LastAtsErrCmd = SC_OperData.AtsCtrlBlckAddr->CmdNumber;

                    /* update the command status index table */
                    SC_OperData.AtsCmdStatusTblAddr[AtsIndex][CmdIndex] = SC_FAILED_CHECKSUM;

                    if (SC_OperData.HkPacket.ContinueAtsOnFailureFlag == false)
                    { /* Stop ATS execution */
                        /*
                         ** Set the temp ATS ID if it is valid
                         */
                        if (SC_OperData.AtsCtrlBlckAddr->AtsNumber == SC_ATSA)
                            TempAtsChar = 'A';
                        else if (SC_OperData.AtsCtrlBlckAddr->AtsNumber == SC_ATSB)
                            TempAtsChar = 'B';

                        /* Mark this ATS for abortion */
                        AbortATS = true;
                    }
                } /* end checksum test */
            }
            else
            { /* the command number does not match */
                /*
                 ** Send an event message to report the invalid command status
                 */

                CFE_EVS_SendEvent(SC_ATS_MSMTCH_ERR_EID, CFE_EVS_EventType_ERROR,
                                  "ATS Command Number Mismatch: Command Skipped, expected: %d received: %d",
                                  (int)SC_ATS_CMD_INDEX_TO_NUM(CmdIndex), Entry->CmdNumber);
                /*
                 ** Increment the ATS error counter
                 */
                SC_OperData.HkPacket.AtsCmdErrCtr++;

                /*
                 ** Update the last ATS error information structure
                 */
                SC_OperData.HkPacket.LastAtsErrSeq = SC_OperData.AtsCtrlBlckAddr->AtsNumber;
                SC_OperData.HkPacket.LastAtsErrCmd = SC_OperData.AtsCtrlBlckAddr->CmdNumber;

                /* update the command status index table */
                SC_OperData.AtsCmdStatusTblAddr[AtsIndex][CmdIndex] = SC_SKIPPED;

                /*
                ** Set the temp ATS ID if it is valid
                */
                if (SC_OperData.AtsCtrlBlckAddr->AtsNumber == SC_ATSA)
                    TempAtsChar = 'A';
                else if (SC_OperData.AtsCtrlBlckAddr->AtsNumber == SC_ATSB)
                    TempAtsChar = 'B';

                /* Mark this ATS for abortion */
                AbortATS = true;
            } /* end if  the command number does not match */
        }
        else /* command isn't marked as loaded */
        {
            /*
             ** Send an event message to report the invalid command status
             */
            CFE_EVS_SendEvent(SC_ATS_SKP_ERR_EID, CFE_EVS_EventType_ERROR,
                              "Invalid ATS Command Status: Command Skipped, Status: %d",
                              SC_OperData.AtsCmdStatusTblAddr[AtsIndex][CmdIndex]);
            /*
             ** Increment the ATS error counter
             */
            SC_OperData.HkPacket.AtsCmdErrCtr++;

            /*
             ** Update the last ATS error information structure
             */
            SC_OperData.HkPacket.LastAtsErrSeq = SC_OperData.AtsCtrlBlckAddr->AtsNumber;
            SC_OperData.HkPacket.LastAtsErrCmd = SC_OperData.AtsCtrlBlckAddr->CmdNumber;

            /* Do Not Mark this ATS for abortion. The command could be marked as EXECUTED
               if we alerady jumped back in time */

        } /* end if */

        if (AbortATS == true)
        {
            CFE_EVS_SendEvent(SC_ATS_ABT_ERR_EID, CFE_EVS_EventType_ERROR, "ATS %c Aborted", TempAtsChar);

            /* Stop the ATS from executing */
            SC_KillAts();
            SC_OperData.AtsCtrlBlckAddr->SwitchPendFlag = false;
        }
        else
        {
            /*
            ** Get the next ATS command set up to execute
            */
            SC_GetNextAtsCommand();
        }

    } /* end if next ATS command time */
} /* end SC_ProcessAtpCmd */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Processes a command from an RTS                                 */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void SC_ProcessRtpCommand(void)
{

    CFE_MSG_Message_t *     BufPtr;      /* a pointer to an RTS entry command */
    SC_RtsEntryHeaderBuf_t *RtsEntryPtr; /* a pointer to an RTS entry header */
    uint16                  RtsIndex;    /* the RTS index for the cmd */
    uint16                  CmdOffset;   /* the location of the cmd    */
    uint32                  Result;
    uint32 *                Buf32;
    bool                    ChecksumValid = false;

    /*
     ** The following conditions must be met before a RTS command is executed:
     ** 1.) The next command time must be <= the current time
     ** 2.) The next processor number must be SC_RTP
     ** 3.) The RTS number in the RTP control block must be valid and
     ** 4.) the RTS must be EXECUTING
     */

    if ((SC_AppData.NextCmdTime[SC_AppData.NextProcNumber] <= SC_AppData.CurrentTime) &&
        (SC_AppData.NextProcNumber == SC_RTP) && (SC_OperData.RtsCtrlBlckAddr->RtsNumber > 0) &&
        (SC_OperData.RtsCtrlBlckAddr->RtsNumber <= SC_NUMBER_OF_RTS) &&
        (SC_OperData.RtsInfoTblAddr[SC_RTS_NUM_TO_INDEX(SC_OperData.RtsCtrlBlckAddr->RtsNumber)].RtsStatus ==
         SC_EXECUTING))
    {
        /*
         ** Count the command for the rate limiter
         ** even if the command fails
         */
        SC_OperData.NumCmdsSec++;

        /* convert the RTS number so that it can be directly indexed into the table*/
        RtsIndex = SC_RTS_NUM_TO_INDEX(SC_OperData.RtsCtrlBlckAddr->RtsNumber);

        /*
         ** Get the Command offset within the RTS
         */
        CmdOffset = SC_OperData.RtsInfoTblAddr[RtsIndex].NextCommandPtr;

        /*
         ** Get a pointer to the RTS entry using the RTS number and the offset
         */
        Buf32       = &SC_OperData.RtsTblAddr[RtsIndex][CmdOffset];
        RtsEntryPtr = (SC_RtsEntryHeaderBuf_t *)Buf32;

        BufPtr = (CFE_MSG_Message_t *)((uint8_t *)RtsEntryPtr + SC_RTS_HEADER_SIZE);
        CFE_MSG_ValidateChecksum(BufPtr, &ChecksumValid);
        if (ChecksumValid == true)
        {
            /*
             ** Try Sending the command on the Software Bus
             */

            Result = CFE_SB_TransmitMsg(BufPtr, true);

            if (Result == CFE_SUCCESS)
            {
                /* the command was sent OK */
                SC_OperData.HkPacket.RtsCmdCtr++;
                SC_OperData.RtsInfoTblAddr[RtsIndex].CmdCtr++;

                /*
                 ** Get the next command.
                 */
                SC_GetNextRtsCommand();
            }
            else
            { /* the software bus return code was bad */

                /*
                 ** Send an event message to report the invalid command status
                 */
                CFE_EVS_SendEvent(SC_RTS_DIST_ERR_EID, CFE_EVS_EventType_ERROR,
                                  "RTS %03d Command Distribution Failed: RTS Stopped. SB returned 0x%08X",
                                  (int)SC_OperData.RtsCtrlBlckAddr->RtsNumber, (unsigned int)Result);

                SC_OperData.HkPacket.RtsCmdErrCtr++;
                SC_OperData.RtsInfoTblAddr[RtsIndex].CmdErrCtr++;
                SC_OperData.HkPacket.LastRtsErrSeq = SC_OperData.RtsCtrlBlckAddr->RtsNumber;
                SC_OperData.HkPacket.LastRtsErrCmd = CmdOffset;

                /*
                 ** Stop the RTS from executing
                 */
                SC_KillRts(RtsIndex);

            } /* end if */
        }
        else
        { /* the checksum failed */

            /*
             ** Send an event message to report the invalid command status
             */
            CFE_EVS_SendEvent(SC_RTS_CHKSUM_ERR_EID, CFE_EVS_EventType_ERROR,
                              "RTS %03d Command Failed Checksum: RTS Stopped",
                              (int)SC_OperData.RtsCtrlBlckAddr->RtsNumber);
            /*
            ** Update the RTS command error counter and last RTS error info
            */
            SC_OperData.HkPacket.RtsCmdErrCtr++;
            SC_OperData.RtsInfoTblAddr[RtsIndex].CmdErrCtr++;
            SC_OperData.HkPacket.LastRtsErrSeq = SC_OperData.RtsCtrlBlckAddr->RtsNumber;
            SC_OperData.HkPacket.LastRtsErrCmd = CmdOffset;

            /*
             ** Stop the RTS from executing
             */
            SC_KillRts(RtsIndex);
        } /* end if */
    }     /* end if */
} /* end SC_ProcessRtpCommand */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/*  Sends Housekeeping Data                                        */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
void SC_SendHkPacket(void)
{
    uint16 i;

    /*
     ** fill in the free bytes in each ATS
     */
    SC_OperData.HkPacket.AtpFreeBytes[SC_ATS_NUM_TO_INDEX(SC_ATSA)] =
        (SC_ATS_BUFF_SIZE32 * SC_BYTES_IN_WORD) -
        (SC_OperData.AtsInfoTblAddr[SC_ATS_NUM_TO_INDEX(SC_ATSA)].AtsSize * SC_BYTES_IN_WORD);
    SC_OperData.HkPacket.AtpFreeBytes[SC_ATS_NUM_TO_INDEX(SC_ATSB)] =
        (SC_ATS_BUFF_SIZE32 * SC_BYTES_IN_WORD) -
        (SC_OperData.AtsInfoTblAddr[SC_ATS_NUM_TO_INDEX(SC_ATSB)].AtsSize * SC_BYTES_IN_WORD);

    /*
     **
     ** fill in the ATP Control Block information
     **
     */

    SC_OperData.HkPacket.AtsNumber = SC_OperData.AtsCtrlBlckAddr->AtsNumber;

    SC_OperData.HkPacket.AtpState       = SC_OperData.AtsCtrlBlckAddr->AtpState;
    SC_OperData.HkPacket.AtpCmdNumber   = SC_OperData.AtsCtrlBlckAddr->CmdNumber;
    SC_OperData.HkPacket.SwitchPendFlag = SC_OperData.AtsCtrlBlckAddr->SwitchPendFlag;

    SC_OperData.HkPacket.NextAtsTime = SC_AppData.NextCmdTime[SC_ATP];

    /*
     ** Fill out the RTP control block information
     */

    SC_OperData.HkPacket.NumRtsActive = SC_OperData.RtsCtrlBlckAddr->NumRtsActive;
    SC_OperData.HkPacket.RtsNumber    = SC_OperData.RtsCtrlBlckAddr->RtsNumber;
    SC_OperData.HkPacket.NextRtsTime  = SC_AppData.NextCmdTime[SC_RTP];

    /*
     ** Fill out the RTS status bit mask
     ** First clear out the status mask
     */
    for (i = 0; i < (SC_NUMBER_OF_RTS + (SC_NUMBER_OF_RTS_IN_UINT16 - 1)) / SC_NUMBER_OF_RTS_IN_UINT16; i++)
    {

        SC_OperData.HkPacket.RtsExecutingStatus[i] = 0;
        SC_OperData.HkPacket.RtsDisabledStatus[i]  = 0;

    } /* end for */

    for (i = 0; i < SC_NUMBER_OF_RTS; i++)
    {

        if (SC_OperData.RtsInfoTblAddr[i].DisabledFlag == true)
        {
            CFE_SET(SC_OperData.HkPacket.RtsDisabledStatus[i / SC_NUMBER_OF_RTS_IN_UINT16],
                    i % SC_NUMBER_OF_RTS_IN_UINT16);
        }
        if (SC_OperData.RtsInfoTblAddr[i].RtsStatus == SC_EXECUTING)
        {
            CFE_SET(SC_OperData.HkPacket.RtsExecutingStatus[i / SC_NUMBER_OF_RTS_IN_UINT16],
                    i % SC_NUMBER_OF_RTS_IN_UINT16);
        }
    } /* end for */

    /* send the status packet */
    CFE_SB_TimeStampMsg(&SC_OperData.HkPacket.TlmHeader.Msg);
    CFE_SB_TransmitMsg(&SC_OperData.HkPacket.TlmHeader.Msg, true);

} /* end SC_SendHkPacket */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Reset Counters Command                                          */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void SC_ResetCountersCmd(const CFE_SB_Buffer_t *BufPtr)
{
    if (SC_VerifyCmdLength(&BufPtr->Msg, sizeof(SC_NoArgsCmd_t)))
    {
        CFE_EVS_SendEvent(SC_RESET_DEB_EID, CFE_EVS_EventType_DEBUG, "Reset counters command");

        SC_OperData.HkPacket.CmdCtr          = 0;
        SC_OperData.HkPacket.CmdErrCtr       = 0;
        SC_OperData.HkPacket.AtsCmdCtr       = 0;
        SC_OperData.HkPacket.AtsCmdErrCtr    = 0;
        SC_OperData.HkPacket.RtsCmdCtr       = 0;
        SC_OperData.HkPacket.RtsCmdErrCtr    = 0;
        SC_OperData.HkPacket.RtsActiveCtr    = 0;
        SC_OperData.HkPacket.RtsActiveErrCtr = 0;
    }
} /* end SC_ResetCountersCmd */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* No Op Command                                                   */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
void SC_NoOpCmd(const CFE_SB_Buffer_t *BufPtr)
{
    if (SC_VerifyCmdLength(&BufPtr->Msg, sizeof(SC_NoArgsCmd_t)))
    {
        SC_OperData.HkPacket.CmdCtr++;
        CFE_EVS_SendEvent(SC_NOOP_INF_EID, CFE_EVS_EventType_INFORMATION, "No-op command. Version %d.%d.%d.%d",
                          SC_MAJOR_VERSION, SC_MINOR_VERSION, SC_REVISION, SC_MISSION_REV);
    }
} /* End SC_NoOpCmd */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/*  Process Requests                                               */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
void SC_ProcessRequest(const CFE_SB_Buffer_t *BufPtr)
{
    CFE_SB_MsgId_t MessageID                      = CFE_SB_INVALID_MSG_ID;
    int8           IsThereAnotherCommandToExecute = false;

    /* cast the packet header pointer on the packet buffer */
    CFE_MSG_GetMsgId(&BufPtr->Msg, &MessageID);

    /*
     ** Get the current system time in the global SC_AppData.CurrentTime
     */
    SC_GetCurrentTime();

    switch (MessageID)
    {
        case SC_CMD_MID:
            /* request from the ground */
            SC_ProcessCommand(BufPtr);
            break;

        case SC_SEND_HK_MID:
            if (SC_VerifyCmdLength(&BufPtr->Msg, sizeof(SC_NoArgsCmd_t)))
            {

                /* set during init to power on or processor reset auto-exec RTS */
                if (SC_AppData.AutoStartRTS != 0)
                {
                    /* make sure the selected auto-exec RTS is enabled */
                    if (SC_OperData.RtsInfoTblAddr[SC_RTS_NUM_TO_INDEX(SC_AppData.AutoStartRTS)].RtsStatus == SC_LOADED)
                    {
                        SC_OperData.RtsInfoTblAddr[SC_RTS_NUM_TO_INDEX(SC_AppData.AutoStartRTS)].DisabledFlag = false;
                    }

                    /* send ground cmd to have SC start the RTS */
                    SC_AutoStartRts(SC_AppData.AutoStartRTS);

                    /* only start it once */
                    SC_AppData.AutoStartRTS = 0;
                }

                /* request from health and safety for housekeeping status */
                SC_SendHkPacket();
            }
            break;

        case SC_1HZ_WAKEUP_MID:
            /*
             ** Time to execute a command in the SC memory
             */
            do
            {
                /*
                 **  Check to see if there is an ATS switch Pending, if so service it.
                 */
                if (SC_OperData.AtsCtrlBlckAddr->SwitchPendFlag == true)
                {
                    SC_ServiceSwitchPend();
                }

                if (SC_AppData.NextProcNumber == SC_ATP)
                {
                    SC_ProcessAtpCmd();
                }
                else
                {
                    if (SC_AppData.NextProcNumber == SC_RTP)
                    {
                        SC_ProcessRtpCommand();
                    }
                }

                SC_UpdateNextTime();
                if ((SC_AppData.NextProcNumber == SC_NONE) ||
                    (SC_AppData.NextCmdTime[SC_AppData.NextProcNumber] > SC_AppData.CurrentTime))
                {
                    SC_OperData.NumCmdsSec         = 0;
                    IsThereAnotherCommandToExecute = false;
                }
                else /* Command needs to run immediately */
                {
                    if (SC_OperData.NumCmdsSec >= SC_MAX_CMDS_PER_SEC)
                    {
                        SC_OperData.NumCmdsSec         = 0;
                        IsThereAnotherCommandToExecute = false;
                    }
                    else
                    {
                        IsThereAnotherCommandToExecute = true;
                    }
                }
            } while (IsThereAnotherCommandToExecute);

            break;

        default:
            CFE_EVS_SendEvent(SC_MID_ERR_EID, CFE_EVS_EventType_ERROR, "Invalid command pipe message ID: 0x%08X",
                              MessageID);

            SC_OperData.HkPacket.CmdErrCtr++;
            break;
    } /* end switch */
} /* end SC_ProcessRequest */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/*  Process a command                                              */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
void SC_ProcessCommand(const CFE_SB_Buffer_t *BufPtr)
{
    CFE_MSG_FcnCode_t CommandCode = 0;
    CFE_SB_MsgId_t    MessageID   = CFE_SB_INVALID_MSG_ID;

    CFE_MSG_GetMsgId(&BufPtr->Msg, &MessageID);
    CFE_MSG_GetFcnCode(&BufPtr->Msg, &CommandCode);

    switch (CommandCode)
    {
        case SC_NOOP_CC:
            SC_NoOpCmd(BufPtr);
            break;

        case SC_RESET_COUNTERS_CC:
            SC_ResetCountersCmd(BufPtr);
            break;

        case SC_START_ATS_CC:
            SC_StartAtsCmd(BufPtr);
            break;

        case SC_STOP_ATS_CC:
            SC_StopAtsCmd(BufPtr);
            break;

        case SC_START_RTS_CC:
            SC_StartRtsCmd(BufPtr);
            break;

        case SC_STOP_RTS_CC:
            SC_StopRtsCmd(BufPtr);
            break;

        case SC_DISABLE_RTS_CC:
            SC_DisableRtsCmd(BufPtr);
            break;

        case SC_ENABLE_RTS_CC:
            SC_EnableRtsCmd(BufPtr);
            break;

        case SC_SWITCH_ATS_CC:
            SC_GroundSwitchCmd(BufPtr);
            break;

        case SC_JUMP_ATS_CC:
            SC_JumpAtsCmd(BufPtr);
            break;

        case SC_CONTINUE_ATS_ON_FAILURE_CC:
            SC_ContinueAtsOnFailureCmd(BufPtr);
            break;

        case SC_APPEND_ATS_CC:
            SC_AppendAtsCmd(BufPtr);
            break;

        case SC_MANAGE_TABLE_CC:
            SC_TableManageCmd(BufPtr);
            break;

#if (SC_ENABLE_GROUP_COMMANDS == true)

        case SC_START_RTSGRP_CC:
            SC_StartRtsGrpCmd(BufPtr);
            break;

        case SC_STOP_RTSGRP_CC:
            SC_StopRtsGrpCmd(BufPtr);
            break;

        case SC_DISABLE_RTSGRP_CC:
            SC_DisableRtsGrpCmd(BufPtr);
            break;

        case SC_ENABLE_RTSGRP_CC:
            SC_EnableRtsGrpCmd(BufPtr);
            break;
#endif

        default:
            CFE_EVS_SendEvent(SC_INVLD_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "Invalid Command Code: MID =  0x%08X CC =  %d", MessageID, CommandCode);
            SC_OperData.HkPacket.CmdErrCtr++;
            break;
    } /* end switch */
} /* end ProcessSequenceRequest */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Table Manage Request Command (sent by cFE Table Services)       */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void SC_TableManageCmd(const CFE_SB_Buffer_t *BufPtr)
{
    int32 ArrayIndex;
    int32 TableID = (int32)((CFE_TBL_NotifyCmd_t *)BufPtr)->Payload.Parameter;

    /* Manage selected table as appropriate for each table type */
    if ((TableID >= SC_TBL_ID_ATS_0) && (TableID < (SC_TBL_ID_ATS_0 + SC_NUMBER_OF_ATS)))
    {
        ArrayIndex = TableID - SC_TBL_ID_ATS_0;
        SC_ManageAtsTable(ArrayIndex);
    }
    else if (TableID == SC_TBL_ID_APPEND)
    {
        SC_ManageAppendTable();
    }
    else if ((TableID >= SC_TBL_ID_RTS_0) && (TableID < (SC_TBL_ID_RTS_0 + SC_NUMBER_OF_RTS)))
    {
        ArrayIndex = TableID - SC_TBL_ID_RTS_0;
        SC_ManageRtsTable(ArrayIndex);
    }
    else if (TableID == SC_TBL_ID_RTS_INFO)
    {
        /* No need to release dump only table pointer */
        CFE_TBL_Manage(SC_OperData.RtsInfoHandle);
    }
    else if (TableID == SC_TBL_ID_RTP_CTRL)
    {
        /* No need to release dump only table pointer */
        CFE_TBL_Manage(SC_OperData.RtsCtrlBlckHandle);
    }
    else if (TableID == SC_TBL_ID_ATS_INFO)
    {
        /* No need to release dump only table pointer */
        CFE_TBL_Manage(SC_OperData.AtsInfoHandle);
    }
    else if (TableID == SC_TBL_ID_APP_INFO)
    {
        /* No need to release dump only table pointer */
        CFE_TBL_Manage(SC_OperData.AppendInfoHandle);
    }
    else if (TableID == SC_TBL_ID_ATP_CTRL)
    {
        /* No need to release dump only table pointer */
        CFE_TBL_Manage(SC_OperData.AtsCtrlBlckHandle);
    }
    else if ((TableID >= SC_TBL_ID_ATS_CMD_0) && (TableID < (SC_TBL_ID_ATS_CMD_0 + SC_NUMBER_OF_ATS)))
    {
        /* No need to release dump only table pointer */
        ArrayIndex = TableID - SC_TBL_ID_ATS_CMD_0;
        CFE_TBL_Manage(SC_OperData.AtsCmdStatusHandle[ArrayIndex]);
    }
    else
    {
        /* Invalid table ID */
        CFE_EVS_SendEvent(SC_TABLE_MANAGE_ID_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Table manage command packet error: table ID = %d", (int)TableID);
    }

    return;

} /* End SC_TableManageCmd() */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Allow cFE Table Services to manage loadable ATS table           */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void SC_ManageRtsTable(int32 ArrayIndex)
{
    int32 Result;

    /* validate RTS array index */
    if (ArrayIndex >= SC_NUMBER_OF_RTS)
    {
        CFE_EVS_SendEvent(SC_TABLE_MANAGE_RTS_INV_INDEX_ERR_EID,
                          CFE_EVS_EventType_ERROR,
                          "RTS table manage error: invalid RTS index %d", 
                          ArrayIndex);
        return;
    }

    /* Release RTS table data pointer */
    CFE_TBL_ReleaseAddress(SC_OperData.RtsTblHandle[ArrayIndex]);

    /* Allow cFE to manage table */
    CFE_TBL_Manage(SC_OperData.RtsTblHandle[ArrayIndex]);

    /* Re-acquire RTS table data pointer */
    Result = CFE_TBL_GetAddress((void *)&SC_OperData.RtsTblAddr[ArrayIndex], SC_OperData.RtsTblHandle[ArrayIndex]);
    if (Result == CFE_TBL_INFO_UPDATED)
    {
        /* Process new RTS table data */
        SC_LoadRts(ArrayIndex);
    }
    else if ((Result != CFE_SUCCESS) && (Result != CFE_TBL_ERR_NEVER_LOADED))
    {
        /* Ignore successful dump or validate and cmds before first activate. */
        CFE_EVS_SendEvent(SC_TABLE_MANAGE_RTS_ERR_EID, CFE_EVS_EventType_ERROR,
                          "RTS table manage process error: RTS = %d, Result = 0x%X",
                          (int)SC_RTS_INDEX_TO_NUM(ArrayIndex), (unsigned int)Result);
    }

    return;

} /* End SC_ManageRtsTable() */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Allow cFE Table Services to manage loadable ATS table           */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void SC_ManageAtsTable(int32 ArrayIndex)
{
    int32 Result;

    /* validate ATS array index */
    if (ArrayIndex >= SC_NUMBER_OF_ATS)
    {
        CFE_EVS_SendEvent(SC_TABLE_MANAGE_ATS_INV_INDEX_ERR_EID,
                          CFE_EVS_EventType_ERROR,
                          "ATS table manage error: invalid ATS index %d", ArrayIndex);
        return;
    }

    /* Release ATS table data pointer */
    CFE_TBL_ReleaseAddress(SC_OperData.AtsTblHandle[ArrayIndex]);

    /* Allow cFE to manage table */
    CFE_TBL_Manage(SC_OperData.AtsTblHandle[ArrayIndex]);

    /* Re-acquire ATS table data pointer */
    Result = CFE_TBL_GetAddress((void *)&SC_OperData.AtsTblAddr[ArrayIndex], SC_OperData.AtsTblHandle[ArrayIndex]);
    if (Result == CFE_TBL_INFO_UPDATED)
    {
        /* Process new ATS table data */
        SC_LoadAts(ArrayIndex);
    }
    else if ((Result != CFE_SUCCESS) && (Result != CFE_TBL_ERR_NEVER_LOADED))
    {
        /* Ignore successful dump or validate and cmds before first activate. */
        CFE_EVS_SendEvent(SC_TABLE_MANAGE_ATS_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ATS table manage process error: ATS = %d, Result = 0x%X",
                          (int)SC_RTS_INDEX_TO_NUM(ArrayIndex), (unsigned int)Result);
    }

    return;

} /* End SC_ManageAtsTable() */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Allow cFE Table Services to manage loadable ATS Append table    */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void SC_ManageAppendTable(void)
{
    int32 Result;

    /* Release ATS Append table data pointer */
    CFE_TBL_ReleaseAddress(SC_OperData.AppendTblHandle);

    /* Allow cFE to manage table */
    CFE_TBL_Manage(SC_OperData.AppendTblHandle);

    /* Re-acquire ATS Append table data pointer */
    Result = CFE_TBL_GetAddress((void *)&SC_OperData.AppendTblAddr, SC_OperData.AppendTblHandle);
    if (Result == CFE_TBL_INFO_UPDATED)
    {
        /* Process new ATS Append table data */
        SC_UpdateAppend();
    }
    else if ((Result != CFE_SUCCESS) && (Result != CFE_TBL_ERR_NEVER_LOADED))
    {
        /* Ignore successful dump or validate and cmds before first activate. */
        CFE_EVS_SendEvent(SC_TABLE_MANAGE_APPEND_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ATS Append table manage process error: Result = 0x%X", (unsigned int)Result);
    }

    return;

} /* End SC_ManageAppendTable() */

/************************/
/*  End of File Comment */
/************************/
