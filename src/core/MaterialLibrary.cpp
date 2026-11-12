#include "core/MaterialLibrary.h"
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>

namespace
{
    QString unquote(QString value)
    {
        value = value.trimmed();
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.size() - 2);
        return value;
    }

    bool parseBool(const QString &value, bool fallback)
    {
        const QString v = unquote(value).toLower();
        if (v == "true" || v == "1")
            return true;
        if (v == "false" || v == "0")
            return false;
        return fallback;
    }

    QColor parseColor(const QString &value, const QColor &fallback)
    {
        const QStringList parts = unquote(value).split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
        if (parts.size() < 3)
            return fallback;
        auto channel = [&](int i, float def)
        {
            bool ok = false;
            const float f = i < parts.size() ? parts[i].toFloat(&ok) : def;
            return std::clamp(ok ? f : def, 0.f, 1.f);
        };
        return QColor::fromRgbF(channel(0, 1.f), channel(1, 1.f), channel(2, 1.f), channel(3, 1.f));
    }

    // Engine text block files: Type { Key = Value ... }
    struct Block
    {
        QString type;
        QHash<QString, QString> values;
    };

    QVector<Block> parseBlocks(const QString &text)
    {
        QVector<Block> blocks;
        Block current;
        bool inBlock = false;
        const QStringList lines = text.split('\n');
        for (QString line : lines)
        {
            const int comment = line.indexOf("//");
            if (comment >= 0)
                line = line.left(comment);
            line = line.trimmed();
            if (line.isEmpty())
                continue;
            if (!inBlock)
            {
                if (line.endsWith('{'))
                {
                    current = Block{line.left(line.size() - 1).trimmed(), {}};
                    inBlock = true;
                }
                continue;
            }
            if (line == "}")
            {
                blocks.push_back(current);
                inBlock = false;
                continue;
            }
            const int eq = line.indexOf('=');
            if (eq > 0)
                current.values.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
        }
        return blocks;
    }

    QImage loadKTex(const QString &path, QString *error)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                *error = file.errorString();
            return {};
        }
        QDataStream in(&file);
        in.setByteOrder(QDataStream::LittleEndian);
        quint32 magic = 0, version = 0, sourceLength = 0;
        in >> magic >> version >> sourceLength;
        if (magic != 0x5845544Bu) // "KTEX"
        {
            if (error)
                *error = "not a .ktex file";
            return {};
        }
        in.skipRawData(int(sourceLength));
        quint32 width = 0, height = 0, format = 0, flags = 0, mips = 0, size = 0;
        in >> width >> height >> format >> flags >> mips >> size;
        if (in.status() != QDataStream::Ok || format != 1 || width == 0 || height == 0 ||
            width > 16384 || height > 16384 || quint64(size) < quint64(width) * height * 4)
        {
            if (error)
                *error = "unsupported .ktex layout";
            return {};
        }
        QImage image(int(width), int(height), QImage::Format_RGBA8888);
        for (quint32 y = 0; y < height; ++y)
        {
            if (in.readRawData(reinterpret_cast<char *>(image.scanLine(int(y))), int(width * 4)) != int(width * 4))
            {
                if (error)
                    *error = "truncated .ktex pixels";
                return {};
            }
        }
        return image;
    }
}

MaterialLibrary::MaterialLibrary(QObject *parent) : QObject(parent) {}

void MaterialLibrary::setRoot(const QString &contentDir, const QString &scanDir, const QString &texturesDir)
{
    if (contentDir == m_root && scanDir == m_scanDir && texturesDir == m_texturesDir && !m_materials.isEmpty())
        return;
    m_root = contentDir;
    m_scanDir = scanDir;
    m_texturesDir = texturesDir;
    reload();
}

void MaterialLibrary::reload()
{
    m_materials.clear();
    m_order.clear();
    if (!m_root.isEmpty() && QDir(m_root).exists())
    {
        const QDir root(m_root);
        const QString scan = !m_scanDir.isEmpty() && QDir(m_scanDir).exists() ? m_scanDir : m_root;
        QDirIterator it(scan, {"*.mat"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString relative = root.relativeFilePath(it.next()).replace('\\', '/');
            m_materials.insert(relative, parse(relative));
            m_order << relative;
        }
        std::sort(m_order.begin(), m_order.end(), [](const QString &a, const QString &b)
                  { return a.compare(b, Qt::CaseInsensitive) < 0; });
    }
    emit changed();
}

const MaterialInfo *MaterialLibrary::find(const QString &path) const
{
    auto it = m_materials.constFind(path);
    return it == m_materials.cend() ? nullptr : &it.value();
}

QSize MaterialLibrary::textureSize(const QString &path) const
{
    const MaterialInfo *info = find(path);
    return info ? info->textureSize() : QSize(64, 64);
}

QImage MaterialLibrary::loadTexture(const QString &absolutePath, QString *error)
{
    if (QFileInfo(absolutePath).suffix().compare("ktex", Qt::CaseInsensitive) == 0)
        return loadKTex(absolutePath, error);
    QImage image(absolutePath);
    if (image.isNull())
    {
        if (error)
            *error = "unreadable image";
        return {};
    }
    return image.convertToFormat(QImage::Format_RGBA8888);
}

MaterialInfo MaterialLibrary::parse(const QString &relativePath) const
{
    MaterialInfo info;
    info.path = relativePath;
    info.name = QFileInfo(relativePath).completeBaseName();

    QFile file(QDir(m_root).filePath(relativePath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return info;
    const QVector<Block> blocks = parseBlocks(QTextStream(&file).readAll());

    QString graphTexture;
    for (const Block &block : blocks)
    {
        if (block.type == "Material")
        {
            if (block.values.contains("BaseColor"))
                info.color = parseColor(block.values["BaseColor"], info.color);
            if (block.values.contains("BaseTexture"))
                info.texturePath = unquote(block.values["BaseTexture"]);
            else if (block.values.contains("Texture"))
                info.texturePath = unquote(block.values["Texture"]);
            if (block.values.contains("Transparent"))
                info.transparent = parseBool(block.values["Transparent"], false);
        }
        else if (block.type == "Node" && unquote(block.values.value("Type")) == "Texture")
        {
            graphTexture = unquote(block.values.value("Asset"));
        }
    }
    if (info.texturePath.isEmpty())
        info.texturePath = graphTexture;

    if (!info.texturePath.isEmpty())
    {
        QString file = QDir(m_root).filePath(info.texturePath);
        if (!QFileInfo::exists(file) && !m_texturesDir.isEmpty())
        {
            // Texture paths written relative to the textures folder ("brick.bmp").
            const QString fallback = QDir(m_texturesDir).filePath(info.texturePath);
            const QString byName = QDir(m_texturesDir).filePath(QFileInfo(info.texturePath).fileName());
            file = QFileInfo::exists(fallback) ? fallback : byName;
        }
        info.texture = loadTexture(file);
    }
    return info;
}
