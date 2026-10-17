/******************************************************************************
Copyright (C) 2014 by Nibbles

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

#include "build/text-freetype2-windows.pch.h"

/* Windows has the GDI+ text source, so this one is deprecated there. */
uint32_t ft2_platform_source_flags(void)
{
	return OBS_SOURCE_DEPRECATED;
}

const char *ft2_platform_default_face(void)
{
	return FT2_DEFAULT_FACE;
}
