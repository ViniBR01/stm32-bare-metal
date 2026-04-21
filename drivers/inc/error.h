#ifndef ERROR_H
#define ERROR_H

/**
 * @file error.h
 * @brief Unified error codes for all drivers.
 *
 * All driver functions that return a status use err_t.
 * Functions with non-error semantic returns (boolean state, field values)
 * keep their original return type (int, uint8_t) but may use ERR_INVALID_ARG
 * for their error path.
 */

typedef enum {
    ERR_OK          =  0,   /**< Success */
    ERR_INVALID_ARG = -1,   /**< Bad parameter (NULL, out of range, etc.) */
    ERR_TIMEOUT     = -2,   /**< Hardware did not become ready in time */
    ERR_BUSY        = -3,   /**< Resource already in use / already allocated */
} err_t;

#endif /* ERROR_H */
