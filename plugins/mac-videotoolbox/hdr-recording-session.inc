/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included after obs_to_vt_transfer. HLG carries its transfer function;
 * scene mastering measurements are not fabricated from a UI peak slider. */
static OSStatus vt_hdr_recording_set_colorspace(VTCompressionSessionRef session)
{
	const void *keys[] = {kVTCompressionPropertyKey_ColorPrimaries, kVTCompressionPropertyKey_TransferFunction,
			     kVTCompressionPropertyKey_YCbCrMatrix};
	const void *values[] = {kCVImageBufferColorPrimaries_ITU_R_2020,
			       kCVImageBufferTransferFunction_ITU_R_2100_HLG, kCVImageBufferYCbCrMatrix_ITU_R_2020};
	CFDictionaryRef properties = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 3,
						       &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (!properties)
		return kVTAllocationFailedErr;
	const OSStatus code = VTSessionSetProperties(session, properties);
	CFRelease(properties);
	return code;
}
