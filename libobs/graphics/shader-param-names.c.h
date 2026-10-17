/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "build/libobs-ported-graphics.pch.h"

enum gs_shader_param_type get_shader_param_type(const char *type)
{
	if (strcmp(type, "float") == 0)
		return GS_SHADER_PARAM_FLOAT;
	else if (strcmp(type, "float2") == 0)
		return GS_SHADER_PARAM_VEC2;
	else if (strcmp(type, "float3") == 0)
		return GS_SHADER_PARAM_VEC3;
	else if (strcmp(type, "float4") == 0)
		return GS_SHADER_PARAM_VEC4;
	else if (strcmp(type, "int2") == 0)
		return GS_SHADER_PARAM_INT2;
	else if (strcmp(type, "int3") == 0)
		return GS_SHADER_PARAM_INT3;
	else if (strcmp(type, "int4") == 0)
		return GS_SHADER_PARAM_INT4;
	else if (astrcmp_n(type, "texture", 7) == 0)
		return GS_SHADER_PARAM_TEXTURE;
	else if (strcmp(type, "float4x4") == 0)
		return GS_SHADER_PARAM_MATRIX4X4;
	else if (strcmp(type, "bool") == 0)
		return GS_SHADER_PARAM_BOOL;
	else if (strcmp(type, "int") == 0)
		return GS_SHADER_PARAM_INT;
	else if (strcmp(type, "string") == 0)
		return GS_SHADER_PARAM_STRING;

	return GS_SHADER_PARAM_UNKNOWN;
}

enum gs_sample_filter get_sample_filter(const char *filter)
{
	if (astrcmpi(filter, "Anisotropy") == 0)
		return GS_FILTER_ANISOTROPIC;

	else if (astrcmpi(filter, "Point") == 0 || strcmp(filter, "MIN_MAG_MIP_POINT") == 0)
		return GS_FILTER_POINT;

	else if (astrcmpi(filter, "Linear") == 0 || strcmp(filter, "MIN_MAG_MIP_LINEAR") == 0)
		return GS_FILTER_LINEAR;

	else if (strcmp(filter, "MIN_MAG_POINT_MIP_LINEAR") == 0)
		return GS_FILTER_MIN_MAG_POINT_MIP_LINEAR;

	else if (strcmp(filter, "MIN_POINT_MAG_LINEAR_MIP_POINT") == 0)
		return GS_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;

	else if (strcmp(filter, "MIN_POINT_MAG_MIP_LINEAR") == 0)
		return GS_FILTER_MIN_POINT_MAG_MIP_LINEAR;

	else if (strcmp(filter, "MIN_LINEAR_MAG_MIP_POINT") == 0)
		return GS_FILTER_MIN_LINEAR_MAG_MIP_POINT;

	else if (strcmp(filter, "MIN_LINEAR_MAG_POINT_MIP_LINEAR") == 0)
		return GS_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;

	else if (strcmp(filter, "MIN_MAG_LINEAR_MIP_POINT") == 0)
		return GS_FILTER_MIN_MAG_LINEAR_MIP_POINT;

	return GS_FILTER_LINEAR;
}

extern enum gs_address_mode get_address_mode(const char *mode)
{
	if (astrcmpi(mode, "Wrap") == 0 || astrcmpi(mode, "Repeat") == 0)
		return GS_ADDRESS_WRAP;
	else if (astrcmpi(mode, "Clamp") == 0 || astrcmpi(mode, "None") == 0)
		return GS_ADDRESS_CLAMP;
	else if (astrcmpi(mode, "Mirror") == 0)
		return GS_ADDRESS_MIRROR;
	else if (astrcmpi(mode, "Border") == 0)
		return GS_ADDRESS_BORDER;
	else if (astrcmpi(mode, "MirrorOnce") == 0)
		return GS_ADDRESS_MIRRORONCE;

	return GS_ADDRESS_CLAMP;
}
