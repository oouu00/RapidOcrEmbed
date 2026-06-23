#ifdef __cplusplus
#ifndef __OCR_LITE_C_API_H__
#define __OCR_LITE_C_API_H__
extern "C"
{

#ifdef WIN32
#ifdef __CLIB__
#define _QM_OCR_API __declspec(dllexport)
#else
#define _QM_OCR_API __declspec(dllimport)
#endif
#else
#define _QM_OCR_API
#endif

typedef void *OCR_HANDLE;
typedef char OCR_BOOL;

#ifndef NULL
#define NULL 0
#endif
#define TRUE 1
#define FALSE 0

typedef struct __ocr_param {
    int padding;
    int maxSideLen;
    float boxScoreThresh;
    float boxThresh;
    float unClipRatio;
    int doAngle; // 1 means do
    int mostAngle; // 1 means true
} OCR_PARAM;

/*
By default, nThreads should be the number of threads
*/
_QM_OCR_API OCR_HANDLE
OcrInit(const char *szDetModel, const char *szClsModel, const char *szRecModel, const char *szKeyPath, int nThreads);

#ifdef __EMBEDDED_MODELS__
_QM_OCR_API OCR_HANDLE
OcrInitEmbedded(int nThreads);
#endif

_QM_OCR_API OCR_BOOL
OcrDetect(OCR_HANDLE handle, const char *imgPath, const char *imgName, OCR_PARAM *pParam);

_QM_OCR_API OCR_BOOL
OcrDetectMem(OCR_HANDLE handle, const unsigned char *imgData, int imgSize, OCR_PARAM *pParam);

_QM_OCR_API int OcrGetLen(OCR_HANDLE handle);

_QM_OCR_API int OcrGetResult(OCR_HANDLE handle, char *szBuf, int nLen);

_QM_OCR_API int OcrGetResultMem(OCR_HANDLE handle, char **szBuf);

_QM_OCR_API void OcrDestroy(OCR_HANDLE handle);

_QM_OCR_API int
OcrDetectEx(OCR_HANDLE handle, const char *imgPath, const char *imgName, OCR_PARAM *pParam);

_QM_OCR_API int
OcrDetectMemEx(OCR_HANDLE handle, const unsigned char *imgData, int imgSize, OCR_PARAM *pParam);

_QM_OCR_API int OcrGetBlockCount(OCR_HANDLE handle);

_QM_OCR_API int OcrGetBlockText(OCR_HANDLE handle, int index, char *szBuf, int nLen);

_QM_OCR_API float OcrGetBlockScore(OCR_HANDLE handle, int index);

_QM_OCR_API int OcrGetBlockBox(OCR_HANDLE handle, int index, int *szBuf);

_QM_OCR_API int OcrGetBlockCharScores(OCR_HANDLE handle, int index, float *szBuf, int nLen);

_QM_OCR_API int OcrGetBlockAngle(OCR_HANDLE handle, int index);

_QM_OCR_API float OcrGetBlockAngleScore(OCR_HANDLE handle, int index);

_QM_OCR_API void OcrSetLayoutStrategy(OCR_HANDLE handle, const char *szStrategy);

_QM_OCR_API int OcrGetLayoutStrategy(OCR_HANDLE handle, char *szBuf, int nLen);

_QM_OCR_API int OcrGetLayoutStrategyCount();

_QM_OCR_API int OcrGetLayoutStrategyInfo(int index, char *szKey, int keyLen, char *szLabel, int labelLen, char *szDesc, int descLen);

_QM_OCR_API OCR_BOOL
OcrInitTable(const char *szTableModel, const char *szTableDict, int nThreads);

#ifdef __EMBEDDED_MODELS__
_QM_OCR_API OCR_HANDLE
OcrInitTableEmbedded(int nThreads);
#endif

_QM_OCR_API void
OcrSetTableMode(OCR_HANDLE handle, int mode);

_QM_OCR_API OCR_BOOL
OcrDetectTable(OCR_HANDLE handle, const char *imgPath, const char *imgName, OCR_PARAM *pParam);

_QM_OCR_API OCR_BOOL
OcrDetectTableMem(OCR_HANDLE handle, const unsigned char *imgData, int imgSize, OCR_PARAM *pParam);

_QM_OCR_API int OcrGetTableLen(OCR_HANDLE handle);

_QM_OCR_API int OcrGetTableResult(OCR_HANDLE handle, char *szBuf, int nLen);

_QM_OCR_API int OcrGetTableResultMem(OCR_HANDLE handle, char **szBuf);

_QM_OCR_API float OcrGetTableStructureScore(OCR_HANDLE handle);

_QM_OCR_API int OcrGetTableOcrText(OCR_HANDLE handle, char *szBuf, int nLen);

_QM_OCR_API int OcrGetTableCellCount(OCR_HANDLE handle);

_QM_OCR_API int OcrGetTableCell(OCR_HANDLE handle, int index, int *x1, int *y1, int *x2, int *y2);

};
#endif //__OCR_LITE_C_API_H__
#endif //__cplusplus
