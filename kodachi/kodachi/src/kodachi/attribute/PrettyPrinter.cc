// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "Attribute.h"
#include <iostream>

namespace {

void printindent(std::ostream& o, unsigned indent) {
    for (int i = 0; i < indent; i++) o << "  ";
}

void printstring(std::ostream& o, const char* s) {
    o << '"';
    while (unsigned char c = *s++) {
        switch (c) {
        case '"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\b': o << "\\b"; break;
        case '\f': o << "\\f"; break;
        case '\n': o << "\\n"; break;
        case '\r': o << "\\r"; break;
        case '\t': o << "\\t"; break;
        default:
            // these are the invalid bytes in UTF-8, and the control characters:
            if (c < 0x20 || c == 0x7F || c == 0xC0 || c == 0xC1 || c >= 0xF5) {
                char buf[5];
                sprintf(buf, "\\x%02X", c);
                o << buf;
            } else {
                o << c;
            }
        }
    }
    o << '"';
}

}

namespace kodachi
{

void print(std::ostream& o, const kodachi::Attribute& attribute, unsigned indent) {

    kodachi::GroupAttribute group(attribute);
    if (group.isValid()) {
        o << '{';
        bool any = false;
        for (auto pair : group) {
            if (any) o << ",\n";
            else {o << '\n'; any = true;}
            printindent(o, indent+1);
            unsigned nesting = 1;
            // nested single-entry groups print as name.name.name: value
            o << pair.name;
            kodachi::Attribute child = pair.attribute;
            for (;;) {
                kodachi::GroupAttribute childGroup(child);
                if (childGroup.isValid() && childGroup.getNumberOfChildren() == 1) {
                    o << '.' << childGroup.getChildNameCStr(0);
                    child = childGroup.getChildByIndex(0);
                    ++nesting;
                } else {
                    break;
                }
            }
            o << ": ";
            print(o, child, indent+nesting);
        }
        if (any) {o << '\n'; printindent(o, indent);}
        o << '}';
        return;
    }

    kodachi::StringAttribute sattr(attribute);
    if (sattr.isValid()) {
        auto strings(sattr.getNearestSample(0));
        if (strings.size() != 1) o << '[';
        if (strings.size() > 4) {
            printstring(o, strings[0]);
            o << ", ...";
        } else {
            for (size_t i = 0; i < strings.size(); ++i) {
                if (i) o << ", ";
                printstring(o, strings[i]);
            }
        }
        if (strings.size() != 1) o << ']';
        return;
    }

    kodachi::IntAttribute iattr(attribute);
    if (iattr.isValid()) {
        auto tuple = iattr.getTupleSize();
        auto values(iattr.getNearestSample(0));
        if (values.size() != tuple) o << '[';
        for (size_t i = 0; i < values.size(); ++i) {
            if (i) o << ", ";
            if (i >= tuple && values.size() > 4) { o << "..."; break; }
            if (tuple > 1 && !(i%tuple)) o << '(';
            o << values[i];
            if (tuple > 1 && !((i+1)%tuple)) o << ')';
        }
        if (values.size() != tuple) o << ']';
        return;
    }

    kodachi::FloatAttribute fattr(attribute);
    if (fattr.isValid()) {
        auto tuple = fattr.getTupleSize();
        auto values(fattr.getNearestSample(0));
        if (values.size() != tuple) o << '[';
        for (size_t i = 0; i < values.size(); ++i) {
            if (i) o << ", ";
            if (i >= tuple && values.size() > 4) { o << "..."; break; }
            if (tuple > 1 && !(i%tuple)) o << '(';
            o << values[i];
            if (tuple > 1 && !((i+1)%tuple)) o << ')';
        }
        if (values.size() != tuple) o << ']';
        return;
    }

    kodachi::DoubleAttribute dattr(attribute);
    if (dattr.isValid()) {
        auto tuple = dattr.getTupleSize();
        auto values(dattr.getNearestSample(0));
        if (values.size() != tuple) o << '[';
        for (size_t i = 0; i < values.size(); ++i) {
            if (i) o << ", ";
            if (i >= tuple && values.size() > 4) { o << "..."; break; }
            if (tuple > 1 && !(i%tuple)) o << '(';
            o << values[i];
            if (tuple > 1 && !((i+1)%tuple)) o << ')';
        }
        if (values.size() != tuple) o << ']';
        return;
    }

    if (kodachi::NullAttribute(attribute).isValid()) {
        o << "null";
        return;
    }

    o << attribute.getXML();
}

}

