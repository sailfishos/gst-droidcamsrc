/*
 * Copyright (C) 2013 Jolla LTD.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "cameraparams.h"
#include <string>
#include <iostream>
#include <sstream>
#include <map>
#include <vector>
#include <cstring>

G_BEGIN_DECLS;

typedef struct {
    std::map<std::string, std::vector<std::string> > items;
} camera_params;

void *camera_params_from_string(const char *str) {
    std::string s(str);

    std::stringstream stream;
    stream.str(s);
    std::string item;
    std::map<std::string, std::vector<std::string> > items;

    while (getline(stream, item, ';')) {
        std::string key, value;
        std::vector<std::string> values;
        std::stringstream i(item);

        if (!getline(i, key, '=')) {
            continue;
        }

        while (getline(i, value, ',')) {
            values.push_back(value);
        }

        if (values.size() == 0) {
            continue;
        }

        items.insert(std::pair<std::string, std::vector<std::string> >(key, values));
    }

    camera_params *params = new camera_params;
    params->items = items;

    return params;
}

void camera_params_free(void *params) {
    camera_params *p = reinterpret_cast<camera_params *>(params);
    delete p;
}

void to_stream(void *p, std::stringstream& stream, char sep) {
    camera_params *params = reinterpret_cast<camera_params *>(p);

    std::map<std::string, std::vector<std::string> >::iterator end = params->items.end();
    --end;

    for (std::map<std::string, std::vector<std::string> >::iterator iter = params->items.begin();
         iter != params->items.end(); iter++) {

        stream << iter->first << "=";

        for (unsigned x = 0; x < iter->second.size(); x++) {
            if (x != 0) {
                stream << ",";
            }

            stream << iter->second[x];
        }

        if (iter != end) {
            stream << sep;
        }
    }
}

char *camera_params_to_string(void *params) {
    std::stringstream s;

    to_stream(params, s, ';');

    return strdup(s.str().c_str());
}

void camera_params_dump(void *params) {
    std::stringstream s;

    to_stream(params, s, '\n');

    std::cout << s.str() << std::endl;
}

void camera_params_set(void *p, const char *key, const char *val) {
    camera_params *params = reinterpret_cast<camera_params *>(p);

    std::vector<std::string> values;

    values.push_back(val);

    params->items.insert(std::pair<std::string, std::vector<std::string> >(key, values));
}

G_END_DECLS;
