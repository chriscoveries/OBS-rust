#pragma once

/* Colour signalling enums shared by the RTMP and FLV outputs (rtmp-stream.h,
 * flv-output.c), declared once so both can share a unity batch. */

/* Adapted from FFmpeg's libavutil/pixfmt.h
 *
 * Renamed to make it apparent that these are not imported as this module does
 * not use or link against FFmpeg.
 */

/**
  * Chromaticity coordinates of the source primaries.
  * These values match the ones defined by ISO/IEC 23091-2_2019 subclause 8.1 and ITU-T H.273.
  */
enum OBSColorPrimaries {
	OBSCOL_PRI_RESERVED0 = 0,
	OBSCOL_PRI_BT709 = 1, ///< also ITU-R BT1361 / IEC 61966-2-4 / SMPTE RP 177 Annex B
	OBSCOL_PRI_UNSPECIFIED = 2,
	OBSCOL_PRI_RESERVED = 3,
	OBSCOL_PRI_BT470M = 4,    ///< also FCC Title 47 Code of Federal Regulations 73.682 (a)(20)
	OBSCOL_PRI_BT470BG = 5,   ///< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R BT1700 625 PAL & SECAM
	OBSCOL_PRI_SMPTE170M = 6, ///< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC
	OBSCOL_PRI_SMPTE240M = 7, ///< identical to above, also called "SMPTE C" even though it uses D65
	OBSCOL_PRI_FILM = 8,      ///< colour filters using Illuminant C
	OBSCOL_PRI_BT2020 = 9,    ///< ITU-R BT2020
	OBSCOL_PRI_SMPTE428 = 10, ///< SMPTE ST 428-1 (CIE 1931 XYZ)
	OBSCOL_PRI_SMPTEST428_1 = OBSCOL_PRI_SMPTE428,
	OBSCOL_PRI_SMPTE431 = 11, ///< SMPTE ST 431-2 (2011) / DCI P3
	OBSCOL_PRI_SMPTE432 = 12, ///< SMPTE ST 432-1 (2010) / P3 D65 / Display P3
	OBSCOL_PRI_EBU3213 = 22,  ///< EBU Tech. 3213-E (nothing there) / one of JEDEC P22 group phosphors
	OBSCOL_PRI_JEDEC_P22 = OBSCOL_PRI_EBU3213,
	OBSCOL_PRI_NB ///< Not part of ABI
};

/**
 * Color Transfer Characteristic.
 * These values match the ones defined by ISO/IEC 23091-2_2019 subclause 8.2.
 */
enum OBSColorTransferCharacteristic {
	OBSCOL_TRC_RESERVED0 = 0,
	OBSCOL_TRC_BT709 = 1, ///< also ITU-R BT1361
	OBSCOL_TRC_UNSPECIFIED = 2,
	OBSCOL_TRC_RESERVED = 3,
	OBSCOL_TRC_GAMMA22 = 4,   ///< also ITU-R BT470M / ITU-R BT1700 625 PAL & SECAM
	OBSCOL_TRC_GAMMA28 = 5,   ///< also ITU-R BT470BG
	OBSCOL_TRC_SMPTE170M = 6, ///< also ITU-R BT601-6 525 or 625 / ITU-R BT1358 525 or 625 / ITU-R BT1700 NTSC
	OBSCOL_TRC_SMPTE240M = 7,
	OBSCOL_TRC_LINEAR = 8,        ///< "Linear transfer characteristics"
	OBSCOL_TRC_LOG = 9,           ///< "Logarithmic transfer characteristic (100:1 range)"
	OBSCOL_TRC_LOG_SQRT = 10,     ///< "Logarithmic transfer characteristic (100 * Sqrt(10) : 1 range)"
	OBSCOL_TRC_IEC61966_2_4 = 11, ///< IEC 61966-2-4
	OBSCOL_TRC_BT1361_ECG = 12,   ///< ITU-R BT1361 Extended Colour Gamut
	OBSCOL_TRC_IEC61966_2_1 = 13, ///< IEC 61966-2-1 (sRGB or sYCC)
	OBSCOL_TRC_BT2020_10 = 14,    ///< ITU-R BT2020 for 10-bit system
	OBSCOL_TRC_BT2020_12 = 15,    ///< ITU-R BT2020 for 12-bit system
	OBSCOL_TRC_SMPTE2084 = 16,    ///< SMPTE ST 2084 for 10-, 12-, 14- and 16-bit systems
	OBSCOL_TRC_SMPTEST2084 = OBSCOL_TRC_SMPTE2084,
	OBSCOL_TRC_SMPTE428 = 17, ///< SMPTE ST 428-1
	OBSCOL_TRC_SMPTEST428_1 = OBSCOL_TRC_SMPTE428,
	OBSCOL_TRC_ARIB_STD_B67 = 18, ///< ARIB STD-B67, known as "Hybrid log-gamma"
	OBSCOL_TRC_NB                 ///< Not part of ABI
};

/**
 * YUV colorspace type.
 * These values match the ones defined by ISO/IEC 23091-2_2019 subclause 8.3.
 */
enum OBSColorSpace {
	OBSCOL_SPC_RGB = 0,   ///< order of coefficients is actually GBR, also IEC 61966-2-1 (sRGB), YZX and ST 428-1
	OBSCOL_SPC_BT709 = 1, ///< also ITU-R BT1361 / IEC 61966-2-4 xvYCC709 / derived in SMPTE RP 177 Annex B
	OBSCOL_SPC_UNSPECIFIED = 2,
	OBSCOL_SPC_RESERVED = 3, ///< reserved for future use by ITU-T and ISO/IEC just like 15-255 are
	OBSCOL_SPC_FCC = 4,      ///< FCC Title 47 Code of Federal Regulations 73.682 (a)(20)
	OBSCOL_SPC_BT470BG =
		5, ///< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R BT1700 625 PAL & SECAM / IEC 61966-2-4 xvYCC601
	OBSCOL_SPC_SMPTE170M =
		6, ///< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC / functionally identical to above
	OBSCOL_SPC_SMPTE240M =
		7, ///< derived from 170M primaries and D65 white point, 170M is derived from BT470 System M's primaries
	OBSCOL_SPC_YCGCO = 8, ///< used by Dirac / VC-2 and H.264 FRext, see ITU-T SG16
	OBSCOL_SPC_YCOCG = OBSCOL_SPC_YCGCO,
	OBSCOL_SPC_BT2020_NCL = 9,          ///< ITU-R BT2020 non-constant luminance system
	OBSCOL_SPC_BT2020_CL = 10,          ///< ITU-R BT2020 constant luminance system
	OBSCOL_SPC_SMPTE2085 = 11,          ///< SMPTE 2085, Y'D'zD'x
	OBSCOL_SPC_CHROMA_DERIVED_NCL = 12, ///< Chromaticity-derived non-constant luminance system
	OBSCOL_SPC_CHROMA_DERIVED_CL = 13,  ///< Chromaticity-derived constant luminance system
	OBSCOL_SPC_ICTCP = 14,              ///< ITU-R BT.2100-0, ICtCp
	OBSCOL_SPC_NB                       ///< Not part of ABI
};
