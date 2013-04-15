/*
Copyright 2012 Uladzimir Pylinski aka barthess.
You may use this work without restrictions, as long as this notice is included.
The work is provided "as is" without warranty of any kind, neither express nor implied.
*/

/*****************************************************************************
 * DATASHEET NOTES
 *****************************************************************************
Write cycle time (byte or page) — 5 ms

Note:
Page write operations are limited to writing
bytes within a single physical page,
regardless of the number of bytes
actually being written. Physical page
boundaries start at addresses that are
integer multiples of the page buffer size (or
‘page size’) and end at addresses that are
integer multiples of [page size – 1]. If a
Page Write command attempts to write
across a physical page boundary, the
result is that the data wraps around to the
beginning of the current page (overwriting
data previously stored there), instead of
being written to the next page as might be
expected.
*********************************************************************/

#include <string.h>

#include "main.h"
#include "eeprom_fs.hpp"

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */

/*
 ******************************************************************************
 * EXTERNS
 ******************************************************************************
 */

/*
 ******************************************************************************
 * PROTOTYPES
 ******************************************************************************
 */

/*
 ******************************************************************************
 * GLOBAL VARIABLES
 ******************************************************************************
 */

/*
 ******************************************************************************
 ******************************************************************************
 * LOCAL FUNCTIONS
 ******************************************************************************
 ******************************************************************************
 */

/**
 *
 */
void EepromFs::toc2buf(const toc_record_t *toc, uint8_t *b){
  uint32_t n = 0;

  memset(b, 0, EEPROM_FS_TOC_RECORD_SIZE);
  strncpy((char*)b, toc->name, EEPROM_FS_TOC_NAME_SIZE);

  n = EEPROM_FS_TOC_NAME_SIZE;
  b[n++] = (toc->inode.startpage >> 8)  & 0xFF;
  b[n++] = toc->inode.startpage & 0xFF;
  b[n++] = (toc->inode.pageoffset >> 8) & 0xFF;
  b[n++] = toc->inode.pageoffset & 0xFF;
  b[n++] = (toc->inode.size >> 8) & 0xFF;
  b[n++] = toc->inode.size & 0xFF;
}

/**
 *
 */
void EepromFs::buf2toc(const uint8_t *b, toc_record_t *toc){
  uint16_t tmp;
  uint32_t n;

  strncpy(toc->name, (const char*)b, EEPROM_FS_TOC_NAME_SIZE);

  n = EEPROM_FS_TOC_NAME_SIZE;
  tmp =  b[n++] << 8;
  tmp |= b[n++];
  toc->inode.startpage = tmp;
  tmp =  b[n++] << 8;
  tmp |= b[n++];
  toc->inode.pageoffset = tmp;
  tmp =  b[n++] << 8;
  tmp |= b[n++];
  toc->inode.size = tmp;
}

/**
 *
 */
size_t EepromFs::inode2absoffset(inodeid_t inodeid, fileoffset_t tip){
  size_t absoffset;
  absoffset =  inode_table[inodeid].startpage * mtd->getPageSize();
  absoffset += inode_table[inodeid].pageoffset + tip;
  return absoffset;
}

/**
 * @brief   Write data that can be fitted in single page boundary
 */
void EepromFs::fitted_write(const uint8_t *data, size_t absoffset,
                            size_t len, uint32_t *written){
  msg_t status = RDY_RESET;

  chDbgCheck(len != 0, "something broken in hi level part");

  status = mtd->write(data, absoffset, len);
  if (status == RDY_OK){
    *written += len;
  }
}

/**
 *
 */
void EepromFs::mkfs(void){
  uint8_t bp[EEPROM_FS_TOC_RECORD_SIZE] = {};
  uint32_t i = 0;
  uint32_t n = 0;
  size_t absoffset = 0;
  msg_t status = RDY_RESET;

  /* erase whole IC */
  this->mtd->massErase();

  /* write all data in single byte transactions to workaround page organized problem */
  for (i=0; i<EEPROM_FS_MAX_FILE_COUNT; i++){
    toc2buf(&(reftoc[i]), bp);
    for (n=0; n<sizeof(bp); n++){
      status = mtd->write(&(bp[n]), absoffset, 1);
      absoffset++;
      chDbgCheck(RDY_OK == status, "MTD broken");
    }
  }
}

/**
 *
 */
bool EepromFs::fsck(void){
  int32_t i = 0;
  msg_t status = RDY_RESET;
  uint8_t bp[EEPROM_FS_TOC_RECORD_SIZE] = {};
  size_t absoffset;

  for (i=0; i<EEPROM_FS_MAX_FILE_COUNT; i++){
    absoffset = EEPROM_FS_TOC_RECORD_SIZE * i;
    status = mtd->read(bp, absoffset, EEPROM_FS_TOC_RECORD_SIZE);
    chDbgCheck(RDY_OK == status, "MTD broken");
    if (0 != strcmp(reftoc[i].name, (const char*)bp))
      return CH_FAILED;
  }
  return CH_SUCCESS;
}

/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */

EepromFs::EepromFs(EepromMtd *mtd, const toc_record_t *reftoc){
  this->ready = false;
  this->mtd = mtd;
  this->reftoc = reftoc;
  chDbgPanic("validate TOC here (overlaping and etc.)");
}

/**
 *
 */
void EepromFs::mount(void){
  uint32_t i = 0;
  msg_t status = RDY_RESET;
  uint8_t bp[EEPROM_FS_TOC_RECORD_SIZE] = {};
  size_t absoffset;
  toc_record_t toc;

  if (CH_FAILED == this->fsck())
    this->mkfs();

  for (i=0; i<EEPROM_FS_MAX_FILE_COUNT; i++){
    absoffset = EEPROM_FS_TOC_RECORD_SIZE * i;
    status = mtd->read(bp, absoffset, EEPROM_FS_TOC_RECORD_SIZE);
    chDbgCheck(RDY_OK == status, "MTD broken");

    buf2toc(bp, &toc);
    inode_table[i].size       = toc.inode.size;
    inode_table[i].pageoffset = toc.inode.pageoffset;
    inode_table[i].startpage  = toc.inode.startpage;
  }

  this->ready = true;
}

/**
 *
 */
size_t EepromFs::getSize(inodeid_t inodeid){
  chDbgCheck(true == this->ready, "Not ready");
  chDbgCheck(EEPROM_FS_MAX_FILE_COUNT > inodeid, "Overflow error");
  return this->inode_table[inodeid].size;
}

/**
 *
 */
inodeid_t EepromFs::findInode(const uint8_t *name){
  (void)name;
  chDbgCheck(true == this->ready, "Not ready");
  chDbgPanic("unrealized");
  return 0;
}

/**
 * @brief     Write data to EEPROM.
 * @details   Only one EEPROM page can be written at once. So fucntion
 *            splits large data chunks in small EEPROM transactions if needed.
 * @note      To achieve the maximum effectivity use write operations
 *            aligned to EEPROM page boundaries.
 * @return    number of processed bytes.
 */
size_t EepromFs::write(const uint8_t *bp, size_t n,
                      inodeid_t inodeid, fileoffset_t tip){

  size_t   len = 0;   /* bytes to be written at one trasaction */
  uint32_t written;   /* total bytes successfully written */
  uint16_t pagesize;
  uint32_t firstpage; /* first page to be affected during transaction */
  uint32_t lastpage;  /* last page to be affected during transaction */
  size_t   absoffset; /* absoulute offset in bytes relative to eeprom start */

  chDbgCheck(true == this->ready, "Not ready");
  chDbgCheck(((tip + n) <= inode_table[inodeid].size), "Overflow error");

  absoffset = inode2absoffset(inodeid, tip);

  pagesize  =  mtd->getPageSize();
  firstpage =  absoffset / pagesize;
  lastpage  =  (absoffset + n - 1) / pagesize;

  written = 0;
  /* data can be fitted in single page */
  if (firstpage == lastpage){
    len = n;
    fitted_write(bp, absoffset, len, &written);
    bp += len;
    absoffset += len;
    return written;
  }

  else{
    /* write first piece of data to first page boundary */
    len =  ((firstpage + 1) * pagesize) - tip;
    len -= inode_table[inodeid].startpage * mtd->getPageSize() + inode_table[inodeid].pageoffset;
    fitted_write(bp, absoffset, len, &written);
    bp += len;
    absoffset += len;

    /* now writes blocks at a size of pages (may be no one) */
    while ((n - written) > pagesize){
      len = pagesize;
      fitted_write(bp, absoffset, len, &written);
      bp += len;
      absoffset += len;
    }

    /* wrtie tail */
    len = n - written;
    if (0 == len)
      return written;
    else{
      fitted_write(bp, absoffset, len, &written);
    }
  }

  return written;
}

/**
 * @brief     Read some bytes from current position in file.
 * @return    number of processed bytes.
 */
size_t EepromFs::read(uint8_t *bp, size_t n, inodeid_t inodeid, fileoffset_t tip){
  msg_t status = RDY_OK;
  size_t absoffset;

  chDbgCheck(true == this->ready, "Not ready");
  chDbgCheck(((tip + n) <= inode_table[inodeid].size), "Overflow error");

  absoffset = inode2absoffset(inodeid, tip);

  /* Stupid I2C cell in STM32F1x does not allow to read single byte.
     So we must read 2 bytes and return needed one. */
#if defined(STM32F1XX_I2C)
  if (n == 1){
    uint8_t __buf[2];
    /* if NOT last byte of file requested */
    if ((getposition(ip) + 1) < getsize(ip)){
      if (read(ip, __buf, 2) == 2){
        lseek(ip, (getposition(ip) + 1));
        bp[0] = __buf[0];
        return 1;
      }
      else
        return 0;
    }
    else{
      lseek(ip, (getposition(ip) - 1));
      if (read(ip, __buf, 2) == 2){
        lseek(ip, (getposition(ip) + 2));
        bp[0] = __buf[1];
        return 1;
      }
      else
        return 0;
    }
  }
#endif /* defined(STM32F1XX_I2C) */

  /* call low level function */
  status = mtd->read(bp, absoffset, n);
  if (status != RDY_OK)
    return 0;
  else
    return n;
}
