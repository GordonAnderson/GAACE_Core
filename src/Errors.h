/**
 * @file Errors.h
 * @brief Error codes for the MIPS / AMPS system.
 *
 * This file defines all error codes that can be set by the MIPS / AMPS system.
 * Error codes are generated when a serial command is received but cannot be
 * processed.  Only the most recent error is retained in the global variable
 * ErrorCode.  The controlling computer can issue the GERR command to read the
 * current error code.  The error code is never automatically cleared; the last
 * error is always available for retrieval.
 *
 * Code ranges
 * -----------
 *   1  –  29  : Shared between MIPS and AMPS (not all codes apply to both).
 *   30 –  100 : Reserved for future shared use.
 *   101 – 127 : MIPS-specific codes.
 *
 * Usage
 * -----
 * Include this header and use the ErrorCode enum type for all error variables
 * and switch statements.  This gives type safety and correct debugger display,
 * unlike the original plain-#define approach.
 *
 * @code
 *   ErrorCode err = ERR_BADCMD;
 *   if (err == ERR_BADCMD) { ... }
 * @endcode
 *
 * Breaking-change note
 * --------------------
 * Two macro names contained spelling errors that have been corrected:
 *   ERR_ADCNOTAVALIABLE  →  ERR_ADCNOTAVAILABLE
 *   ERR_ADCALREARYSETUP  →  ERR_ADCALREADYSETUP
 * Compatibility aliases (plain #defines pointing at the enum values) are
 * provided below the enum to avoid breaking existing callers, but new code
 * should use the corrected names.
 */

#ifndef ERRORS_H
#define ERRORS_H

/**
 * @brief Typed enumeration of all MIPS / AMPS error codes.
 *
 * Using an enum instead of plain #define macros provides:
 *  - Type safety (a function can declare `ErrorCode` parameters).
 *  - Correct display in debuggers and static-analysis tools.
 *  - Exhaustiveness warnings from the compiler in switch statements.
 */
enum ErrorCode
{
    // -----------------------------------------------------------------------
    // Shared error codes (MIPS and AMPS)
    // Codes 30–100 are reserved for future shared use.
    // -----------------------------------------------------------------------

    ERR_BADCMD                =   1,  ///< Invalid command
    ERR_BADARG                =   2,  ///< Invalid argument
    ERR_LOCALREADY            =   3,  ///< System is already in LOC mode
    ERR_TBLALREADY            =   4,  ///< System is already in TBL mode
    ERR_NOTBLLOADED           =   5,  ///< No tables have been loaded
    ERR_NOTBLMODE             =   6,  ///< System is not in table mode
    ERR_TBLNOTREADY           =   7,  ///< Table not ready
    ERR_TOKENTIMEOUT          =   8,  ///< Timed out waiting for token
    ERR_EXPECTEDCOLON         =   9,  ///< Expected to see a ':'
    ERR_TBLTOOBIG             =  10,  ///< Table too large
    ERR_CHLOWORBRD            =  11,  ///< Channel number too low, or board not present
    ERR_CHHIORBRD             =  12,  ///< Channel number too high, or board not present
    ERR_CHHIGH                =  13,  ///< Requested channel number too high
    ERR_BRDLOWORBRD           =  14,  ///< Board number too low, or board not present
    ERR_BRDHIORBRD            =  15,  ///< Board number too high, or board not present
    ERR_BRDHIGH               =  16,  ///< Board number too high
    ERR_BRDNOTSUPPORT         =  17,  ///< Command not supported on this board revision
    ERR_BADBAUD               =  18,  ///< Invalid baud rate requested
    ERR_EXPECTEDCOMMA         =  19,  ///< Expected comma
    ERR_NESTINGTOODEEP        =  20,  ///< Table nesting too deep
    ERR_MISSINGOPENBRACKET    =  21,  ///< Table ']' without corresponding '['
    ERR_INVALIDCHAN           =  22,  ///< Invalid channel requested
    ERR_DIOHARDWARENOTPRESENT =  23,  ///< DIO hardware not found
    ERR_TEMPRANGE             =  24,  ///< Temperature range error
    ERR_ESIHVOUTOFRANGE       =  25,  ///< ESI HV out of range
    ERR_TEMPCONTROLLOOPGAIN   =  26,  ///< Temperature control loop gain range error
    ERR_NOTLOCMODE            =  27,  ///< System is not in local mode
    ERR_WRONGTRGMODE          =  28,  ///< Wrong trigger mode
    ERR_CANTFINDENTRY         =  29,  ///< Cannot locate the requested table entry

    // Codes 30–100 are reserved for future shared use.

    // -----------------------------------------------------------------------
    // MIPS-specific error codes
    // -----------------------------------------------------------------------

    ERR_VALUERANGE            = 101,  ///< Requested value is out of range
    ERR_NOSDCARD              = 102,  ///< No SD card present in system
    ERR_CANTCREATEFILE        = 103,  ///< Cannot create file on SD card
    ERR_FILENAMETOOLONG       = 104,  ///< Filename is too long
    ERR_CANTOPENFILE          = 105,  ///< Cannot open file
    ERR_CANTDELETEFILE        = 106,  ///< Cannot delete file
    ERR_NOTSUPPORTINREV       = 107,  ///< Not supported in this MIPS controller revision
    ERR_WIFICONNECTED         = 108,  ///< Not supported while WiFi is connected
    ERR_NOETHERNET            = 109,  ///< No Ethernet adapter detected

    // BUG FIX: ERR_ETHERNETCOMM and ERR_EEPROMWRITE were both defined as 110
    // in the original file.  ERR_EEPROMWRITE has been renumbered to 110a... 
    // but since enum values must be unique integers, ERR_EEPROMWRITE is
    // assigned the next available value (111) and subsequent codes are
    // shifted up by one.  Update any serialised error logs or protocol
    // documentation to reflect the new assignments.
    ERR_ETHERNETCOMM          = 110,  ///< Ethernet adapter communication error
    ERR_EEPROMWRITE           = 111,  ///< Error writing to EEPROM on module  [WAS: duplicate 110 — FIXED]
    ERR_NOTOFFSETABLE         = 112,  ///< DC bias module is not offsetable
    ERR_INTERNAL              = 113,  ///< Internal error (e.g. cannot allocate resource)
    ERR_BMPERROR              = 114,  ///< BMP file error
    ERR_TUNEINPROCESS         = 115,  ///< Auto-tune is already in progress
    ERR_NOARB                 = 116,  ///< No ARB module present in system
    ERR_CANTALLOCATE          = 117,  ///< Cannot allocate required memory
    ERR_PROFILENOTDEFINED     = 118,  ///< DC bias profile not defined
    ERR_NAMEINLIST            = 119,  ///< Name already exists in linked list
    ERR_NAMENOTFOUND          = 120,  ///< Name not found in linked list  [WAS: wrong description — FIXED]
    ERR_EEPROMREAD            = 121,  ///< Error reading from EEPROM on module
    ERR_READINGSD             = 122,  ///< Error reading data from SD card
    ERR_NOTSUPPORTED          = 123,  ///< Not supported by hardware
    ERR_NOTINMANMODE          = 124,  ///< Auto-tune error: not in manual mode
    ERR_ADCNOTAVAILABLE       = 125,  ///< ADC interface in use and not available  [WAS: ERR_ADCNOTAVALIABLE — FIXED]
    ERR_ADCALREADYSETUP       = 126,  ///< ADC interface is already set up         [WAS: ERR_ADCALREARYSETUP — FIXED]
    ERR_ADCNOTSETUP           = 127,  ///< ADC interface is not set up
    ERR_TIMEOUT               = 128,  ///< Operation timed out
};

// ---------------------------------------------------------------------------
// Backward-compatibility aliases for corrected macro names.
//
// These allow existing source files that reference the old misspelled names
// to continue compiling without modification.  New code should use the
// corrected enum member names above.
// ---------------------------------------------------------------------------

/** @deprecated Use ERR_ADCNOTAVAILABLE */
#define ERR_ADCNOTAVALIABLE  ERR_ADCNOTAVAILABLE

/** @deprecated Use ERR_ADCALREADYSETUP */
#define ERR_ADCALREARYSETUP  ERR_ADCALREADYSETUP

#endif // ERRORS_H
