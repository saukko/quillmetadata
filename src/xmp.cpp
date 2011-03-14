/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Contact: Alexander Bokovoy <alexander.bokovoy@nokia.com>
**
** This file is part of the Quill Metadata package.
**
** Commercial Usage
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain
** additional rights. These rights are described in the Nokia Qt LGPL
** Exception version 1.0, included in the file LGPL_EXCEPTION.txt in this
** package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at qt-sales@nokia.com.
**
****************************************************************************/

#include <QStringList>
#include <QLocale>
#include <QTextStream>
#include <exempi-2.0/exempi/xmpconsts.h>
#include <math.h>

#include "xmp.h"

QHash<QuillMetadata::Tag,XmpTag> Xmp::m_xmpTags;
bool Xmp::m_initialized = false;

XmpTag::XmpTag(const QString &schema, const QString &tag, TagType tagType) :
    schema(schema), tag(tag), tagType(tagType)
{
}

Xmp::Xmp()
{
    m_xmpPtr = xmp_new_empty();
}

Xmp::Xmp(const QString &fileName)
{
    xmp_init();
    XmpFilePtr xmpFilePtr = xmp_files_open_new(fileName.toAscii().constData(),
                                               XMP_OPEN_READ);
    m_xmpPtr = xmp_files_get_new_xmp(xmpFilePtr);
    xmp_files_close(xmpFilePtr, XMP_CLOSE_NOOPTION);
    xmp_files_free(xmpFilePtr);

    initTags();
}

Xmp::~Xmp()
{
    xmp_free(m_xmpPtr);
}

bool Xmp::isValid() const
{
    return (m_xmpPtr != 0);
}

bool Xmp::supportsEntry(QuillMetadata::Tag tag) const
{
    return m_xmpTags.contains(tag);
}

bool Xmp::hasEntry(QuillMetadata::Tag tag) const
{
    return (supportsEntry(tag) &&
            !m_xmpTags.values(tag).isEmpty());
}

QString Xmp::processXmpString(XmpStringPtr xmpString)
{
    return QString(xmp_string_cstr(xmpString)).trimmed();
}

QVariant Xmp::entry(QuillMetadata::Tag tag) const
{
    if (!supportsEntry(tag))
        return QVariant();

    QList<XmpTag> xmpTags = m_xmpTags.values(tag);

    XmpStringPtr xmpStringPtr = xmp_string_new();

    foreach (XmpTag xmpTag, xmpTags) {
        uint32_t propBits;

        if (xmp_get_property(m_xmpPtr,
                             xmpTag.schema.toAscii().constData(),
                             xmpTag.tag.toAscii().constData(),
                             xmpStringPtr,
                             &propBits)) {
            if (XMP_IS_PROP_ARRAY(propBits)) {
                QStringList list;
                int i = 1;
                while (xmp_get_array_item(m_xmpPtr,
                                          xmpTag.schema.toAscii().constData(),
                                          xmpTag.tag.toAscii().constData(),
                                          i,
                                          xmpStringPtr,
                                          &propBits)) {
                    QString string = processXmpString(xmpStringPtr);
                    if (!string.isEmpty())
                        list << string;
                    i++;
                }

                if (!list.isEmpty()) {
                    xmp_string_free(xmpStringPtr);
                    return QVariant(list);
                }
            }
            else {
                QString string = processXmpString(xmpStringPtr);
                switch (tag) {
                    case QuillMetadata::Tag_GPSLatitude:
                    case QuillMetadata::Tag_GPSLongitude: {
                        // Degrees and minutes are separated with a ','
                        QStringList elements = string.split(",");
                        QLocale c(QLocale::C);
                        double value = 0, term = 0;
                        for (int i = 0, power = 1; i < elements.length(); i ++, power *= 60) {
                            term = c.toDouble(elements[i]);
                            if (i == elements.length() - 1) {
                                term = c.toDouble(elements[i].mid(0, elements[i].length() - 2));
                            }
                            value += term / power;
                        }

                        return QVariant(value);
                    }

                    case QuillMetadata::Tag_GPSLatitudeRef:
                    case QuillMetadata::Tag_GPSLongitudeRef: {
                        // The 'W', 'E', 'N' or 'S' character is the rightmost character
                        // in the field
                        return QVariant(string.right(1));
                    }

                    case QuillMetadata::Tag_GPSImgDirection:
                    case QuillMetadata::Tag_GPSAltitude: {
                        const int separator = string.indexOf("/");
                        const int len = string.length();
                        QLocale c(QLocale::C);

                        double numerator = c.toDouble(string.mid(0, separator));
                        double denominator = c.toDouble(string.mid(separator + 1, len - separator - 1));

                        if (denominator && separator != -1) {
                            return QVariant(numerator / denominator);
                        }
                        else {
                            return QVariant(numerator);
                        }
                    }

                    default:
                        if (!string.isEmpty()) {
                            xmp_string_free(xmpStringPtr);
                            return QVariant(string);
                        }
                        break;
                }
            }
        }
    }
    xmp_string_free(xmpStringPtr);
    return QVariant();
}

void Xmp::setEntry(QuillMetadata::Tag tag, const QVariant &entry)
{
    if (!supportsEntry(tag))
        return;

    if (!m_xmpPtr) {
        m_xmpPtr = xmp_new_empty();
    }

    switch (tag) {
        case QuillMetadata::Tag_GPSLatitude:
        case QuillMetadata::Tag_GPSLongitude: {
            QString value = entry.toString();
            const QString refPositive(tag == QuillMetadata::Tag_GPSLatitude ? "N" : "E");
            const QString refNegative(tag == QuillMetadata::Tag_GPSLatitude ? "S" : "W");

            if (!value.contains(",")) {
                double val = value.toDouble();
                QString parsedEntry;
                val = fabs(val);
                double remains = (val - trunc(val)) * 3600;

                QTextStream(&parsedEntry) << trunc(val) << ","
                        << trunc(remains / 60) << ","
                        << remains - trunc(remains / 60) * 60;

                if (value.startsWith("-")) {
                    parsedEntry.append(refNegative);
                }
                else {
                    parsedEntry.append(refPositive);
                }

                setXmpEntry(tag, QVariant(parsedEntry));
            }
            else if (value.endsWith(refPositive) || value.endsWith(refNegative)) {
                if (value.startsWith("-")) {
                    value.replace(value.right(1), refNegative);
                    setXmpEntry(tag, QVariant(value.mid(1)));
                }
            } else {
                value.append(value.startsWith("-") ? refNegative : refPositive);
                setXmpEntry(tag, QVariant(value));
            }

            break;
        }
        default:
            setXmpEntry(tag, entry);
            break;
    }
}

void Xmp::setXmpEntry(QuillMetadata::Tag tag, const QVariant &entry)
{
    QList<XmpTag> xmpTags = m_xmpTags.values(tag);

    foreach (XmpTag xmpTag, xmpTags) {
        xmp_delete_property(m_xmpPtr,
                            xmpTag.schema.toAscii().constData(),
                            xmpTag.tag.toAscii().constData());

        if (xmpTag.tagType == XmpTag::TagTypeString)
            xmp_set_property(m_xmpPtr,
                             xmpTag.schema.toAscii().constData(),
                             xmpTag.tag.toAscii().constData(),
                             entry.toByteArray().constData(), 0);
        else if (xmpTag.tagType == XmpTag::TagTypeStringList) {
            QStringList list = entry.toStringList();
            foreach (QString string, list)
                xmp_append_array_item(m_xmpPtr,
                                      xmpTag.schema.toAscii().constData(),
                                      xmpTag.tag.toAscii().constData(),
                                      XMP_PROP_ARRAY_IS_UNORDERED,
                                      string.toUtf8().constData(), 0);
        }
        else if (xmpTag.tagType == XmpTag::TagTypeAltLang) {
            xmp_set_localized_text(m_xmpPtr,
                                   xmpTag.schema.toAscii().constData(),
                                   xmpTag.tag.toAscii().constData(),
                                   "", "x-default",
                                   entry.toByteArray().constData(), 0);
        }
    }
}

void Xmp::removeEntry(QuillMetadata::Tag tag)
{
    if (!supportsEntry(tag))
        return;

    if (!m_xmpPtr)
        return;

    QList<XmpTag> xmpTags = m_xmpTags.values(tag);

    foreach (XmpTag xmpTag, xmpTags) {
        xmp_delete_property(m_xmpPtr,
                            xmpTag.schema.toAscii().constData(),
                            xmpTag.tag.toAscii().constData());
    }
}

bool Xmp::write(const QString &fileName) const
{
    XmpPtr ptr = m_xmpPtr;

    if (!ptr)
        ptr = xmp_new_empty();

    XmpFilePtr xmpFilePtr = xmp_files_open_new(fileName.toAscii().constData(),
                                               XMP_OPEN_FORUPDATE);
    bool result;

    if (xmp_files_can_put_xmp(xmpFilePtr, ptr))
        result = xmp_files_put_xmp(xmpFilePtr, ptr);
    else
        result = false;

    // Crash safety can be ignored here by selecting Nooption since
    // QuillFile already has crash safety measures.
    if (result)
        result = xmp_files_close(xmpFilePtr, XMP_CLOSE_NOOPTION);
    xmp_files_free(xmpFilePtr);

    if (!m_xmpPtr)
        xmp_free(ptr);

    return result;
}

void Xmp::initTags()
{
    if (m_initialized)
        return;

    m_initialized = true;

    m_xmpTags.insertMulti(QuillMetadata::Tag_Creator,
                          XmpTag(NS_DC, "creator", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_Subject,
                          XmpTag(NS_DC, "subject", XmpTag::TagTypeStringList));
    m_xmpTags.insertMulti(QuillMetadata::Tag_City,
                          XmpTag(NS_PHOTOSHOP, "City", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_Country,
                          XmpTag(NS_PHOTOSHOP, "Country", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_City,
                          XmpTag(NS_IPTC4XMP, "LocationShownCity", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_Country,
                          XmpTag(NS_IPTC4XMP, "LocationShownCountry", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_Location,
                          XmpTag(NS_IPTC4XMP, "LocationShownSublocation", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_Rating,
                          XmpTag(NS_XAP, "Rating", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_Timestamp,
                          XmpTag(NS_XAP, "MetadataDate", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_Description,
                          XmpTag(NS_DC, "description", XmpTag::TagTypeAltLang));
    m_xmpTags.insertMulti(QuillMetadata::Tag_Orientation,
                          XmpTag(NS_EXIF, "Orientation", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_Orientation,
                          XmpTag(NS_TIFF, "Orientation", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_Title,
                          XmpTag(NS_DC, "title", XmpTag::TagTypeAltLang));

    m_xmpTags.insertMulti(QuillMetadata::Tag_GPSLatitude,
                          XmpTag(NS_EXIF, "GPSLatitude", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_GPSLongitude,
                          XmpTag(NS_EXIF, "GPSLongitude", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_GPSAltitude,
                          XmpTag(NS_EXIF, "GPSAltitude", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_GPSAltitudeRef,
                          XmpTag(NS_EXIF, "GPSAltitudeRef", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_GPSImgDirection,
                          XmpTag(NS_EXIF, "GPSImgDirection", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_GPSImgDirectionRef,
                          XmpTag(NS_EXIF, "GPSImgDirectionRef", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_GPSVersionID,
                          XmpTag(NS_EXIF, "GPSVersionID", XmpTag::TagTypeString));

    // Workaround for missing reference tags: we'll extract them from the
    // Latitude and Longitude tags
    m_xmpTags.insertMulti(QuillMetadata::Tag_GPSLatitudeRef,
                              XmpTag(NS_EXIF, "GPSLatitude", XmpTag::TagTypeString));
    m_xmpTags.insertMulti(QuillMetadata::Tag_GPSLongitudeRef,
                              XmpTag(NS_EXIF, "GPSLongitude", XmpTag::TagTypeString));
}
