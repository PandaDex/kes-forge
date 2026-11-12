#include "assets/AssetTypeMap.h"

// ---------------------------------------------------------------------------------------------
// Which program opens each asset type: "extension" -> "program".
//
//   "@forge"         open in this editor (maps)
//   "@system"        the OS default application
//   "name"           found in kToolSearchDirs, next to forge.exe, then on PATH
//   "dir/name.exe"   relative to the .kproject folder, or an absolute path
//   "a|b"            first program that is found wins
//
// Arguments may follow the program. {file} is the asset's absolute path (added at the end when
// missing), {content} the content root, {root} the game root (folder holding the content
// folder), {project} the .kproject path.
// "code {root} {file}" opens the file in VS Code with the game root as the workspace.
// A program that can't be found is asked for once and remembered (QSettings "assetEditors/<name>").
// ---------------------------------------------------------------------------------------------
const QMap<QString, QString> kAssetEditors = {
    { "fmap", "@forge" },
    { "mat", "kesShaderGraph" },
    { "slangnodes", "kesShaderGraph" },

    { "rml", "code {root} {file}|notepad" },
    { "rcss", "code {root} {file}|notepad" },
    { "slang", "code {root} {file}|notepad" },
    { "shader", "code {root} {file}|notepad" },
    { "dec", "code {root} {file}|notepad" },
    { "kproject", "code {root} {file}|notepad" },
    { "ini", "code {root} {file}|notepad" },
    { "txt", "code {root} {file}|notepad" },
    { "imap", "code {root} {file}|notepad" },

    { "png", "@system" },
    { "jpg", "@system" },
    { "jpeg", "@system" },
    { "bmp", "@system" },
    { "tif", "@system" },
    { "tiff", "@system" },
    { "wav", "@system" },
    { "mp3", "@system" },
    { "ogg", "@system" },
    { "gltf", "@system" },
    { "glb", "@system" },
    { "fbx", "@system" },
    { "obj", "@system" },

    // {"kmodel",  "modelViewer {file}"},
};

const QStringList kToolSearchDirs = {
    ".",
    "tools",
    "../out/build/dev/engine/tools/model-importer/Debug",
    "../out/build/dev/engine/tools/model-importer/Release",
    "../out/build/dev/engine/tools/model-importer/RelWithDebInfo",
    "../tools/shaderGraph/build/Debug",
    "../tools/shaderGraph/build/Release",
};

const QString kImporterProgram = "model-importer";

// Animations in a glTF/FBX file are written next to the model as <model>_<clip>.kanim.
const QList<ImportRule> kImportRules = {
    { "Models and animations", { "fbx", "gltf", "glb", "obj" }, "kmodel", "meshes" },
    { "Textures", { "png", "jpg", "jpeg", "bmp", "tif", "tiff" }, "ktex", "textures" },
    { "Sounds", { "wav", "mp3", "ogg" }, "ksound", "sounds" },
};
