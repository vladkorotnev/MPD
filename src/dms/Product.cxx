/*
 * Copyright (C) 2003-2017 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
 
#include "config.h"
#include "Product.hxx"
#include "config/ConfigOption.hxx"
#include "config/ConfigGlobal.hxx"
#include "util/ASCII.hxx"
#include "util/Macros.hxx"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

DmsProduct mDmsProduct;

const char *product_tbl[] = {
	"DMS-500",
	"AiOS",
	"H8",
	nullptr,
};

const char *
DmsProduct::c_str() const
{
	assert((unsigned)type < ARRAY_SIZE(product_tbl));

	return product_tbl[type];
}

void DmsProductInit()
{
	const char *name = config_get_string(ConfigOption::PRODUCT_NAME,
					product_tbl[0]);

	for (unsigned i=0;i<ARRAY_SIZE(product_tbl);i++) {
		if (strcmp(name, product_tbl[i]) == 0) {
			mDmsProduct = DmsProduct((DmsProduct::TYPE)i);
			break;
		}
	}
}

namespace Dms {

static const char *product_tbl[] = {
	"DMS-500",
	"AiOS",
	"H8",
};

const char *
Product::getVender() const
{
	return "Cary Audio";
}

const char *
Product::getProduct() const
{
	assert(type < ARRAY_SIZE(product_tbl));

	return product_tbl[type];
}

const char *
Product::c_str() const
{
	assert(type < ARRAY_SIZE(product_tbl));

	return product_tbl[type];
}

std::string
Product::fullName() const
{
	std::string str = "CARY ";
	str.append(c_str());

	return str;
}

bool
Product::init()
{
	assert(type < ARRAY_SIZE(product_tbl));

	const char *name = config_get_string(ConfigOption::PRODUCT_NAME,
					product_tbl[type]);

	for (unsigned i=0;i<ARRAY_SIZE(product_tbl);i++) {
		if (StringEqualsCaseASCII(name, product_tbl[i])) {
			type = i;
			return true;
		}
	}

	return false;
}

}
