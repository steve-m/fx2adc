/*
 * fx2adc - acquire data from Cypress FX2 + AD9288 based USB scopes
 *
 * Copyright (C) 2012 by Hoernchen <la@tfc-server.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FX2ADC_EXPORT_H
#define FX2ADC_EXPORT_H

#if defined __GNUC__
#  if __GNUC__ >= 4
#    define __FX2ADC_EXPORT   __attribute__((visibility("default")))
#    define __FX2ADC_IMPORT   __attribute__((visibility("default")))
#  else
#    define __FX2ADC_EXPORT
#    define __FX2ADC_IMPORT
#  endif
#elif _MSC_VER
#  define __FX2ADC_EXPORT     __declspec(dllexport)
#  define __FX2ADC_IMPORT     __declspec(dllimport)
#else
#  define __FX2ADC_EXPORT
#  define __FX2ADC_IMPORT
#endif

#ifndef fx2adc_STATIC
#	ifdef fx2adc_EXPORTS
#	define FX2ADC_API __FX2ADC_EXPORT
#	else
#	define FX2ADC_API __FX2ADC_IMPORT
#	endif
#else
#define FX2ADC_API
#endif
#endif /* FX2ADC_EXPORT_H */
