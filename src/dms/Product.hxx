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
 
#pragma once

class DmsProduct {
public:
	typedef enum { DMS500, AIOS, H8, MAX } TYPE;

	DmsProduct(TYPE t=DMS500):type(t) {}

	bool isDMS500() const {
		return type == DMS500;
	}

	bool isAIOS() const {
		return type == AIOS;
	}

	bool isH8() const {
		return type == H8;
	}

	TYPE getType() const {
		return type;
	}

	const char *c_str() const;

private:
	TYPE type;
};

void DmsProductInit();

extern DmsProduct mDmsProduct;

extern const char *product_tbl[];


#include "Compiler.h"

#include <string>

namespace Dms {

struct Product {
	enum { DMS500, AIOS, H8, MAX };

	unsigned type;

	Product():type(AIOS) {}

	gcc_pure
	const char *c_str() const;

	gcc_pure
	std::string fullName() const;

	gcc_pure
	bool isDMS500() const {
		return type == DMS500;
	}

	gcc_pure
	bool isAIOS() const {
		return type == AIOS;
	}

	gcc_pure
	bool isH8() const {
		return type == H8;
	}

	const char *getVender() const;

	const char *getProduct() const;

	bool init();
};

}
