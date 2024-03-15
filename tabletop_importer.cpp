/*
    tabletop_club_godot_module
    Copyright (c) 2020-2021 Benjamin 'drwhut' Beddows

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include "tabletop_importer.h"

#include "core/os/os.h"
#include "editor/import/editor_import_collada.h"
#include "editor/import/resource_importer_obj.h"
#include "editor/import/resource_importer_scene.h"
#include "editor/import/resource_importer_texture.h"
#include "editor/import/resource_importer_wav.h"
#include "modules/gltf/editor_scene_importer_gltf.h"
#include "modules/minimp3/resource_importer_mp3.h"
#include "modules/stb_vorbis/resource_importer_ogg_vorbis.h"

TabletopImporter::TabletopImporter() {
    if (!ResourceImporterTexture::get_singleton()) {
        Ref<ResourceImporterTexture> texture_importer;
        texture_importer.instance();
        ResourceFormatImporter::get_singleton()->add_importer(texture_importer);
    }

    if (!ResourceImporterScene::get_singleton()) {
        Ref<ResourceImporterScene> scene_importer;
        scene_importer.instance();
        ResourceFormatImporter::get_singleton()->add_importer(scene_importer);

        Ref<EditorSceneImporterCollada> collada_importer;
        collada_importer.instance();
        scene_importer->add_importer(collada_importer);

        // Ref<EditorSceneImporterGLTF> gltf_importer;
        // gltf_importer.instance();
        // scene_importer->add_importer(gltf_importer);

        Ref<EditorOBJImporter> obj_importer;
        obj_importer.instance();
        scene_importer->add_importer(obj_importer);
    }

    if (ResourceFormatImporter::get_singleton()->get_importer_by_name("wav").is_null()) {
        Ref<ResourceImporterWAV> wav_importer;
        wav_importer.instance();
        ResourceFormatImporter::get_singleton()->add_importer(wav_importer);
    }

    if (ResourceFormatImporter::get_singleton()->get_importer_by_name("ogg_vorbis").is_null()) {
        Ref<ResourceImporterOGGVorbis> ogg_importer;
        ogg_importer.instance();
        ResourceFormatImporter::get_singleton()->add_importer(ogg_importer);
    }

    if (ResourceFormatImporter::get_singleton()->get_importer_by_name("mp3").is_null()) {
        Ref<ResourceImporterMP3> mp3_importer;
        mp3_importer.instance();
        ResourceFormatImporter::get_singleton()->add_importer(mp3_importer);
    }
}

TabletopImporter::~TabletopImporter() {}

Error TabletopImporter::copy_file(const String &p_from, const String &p_to, bool force) {

    ERR_FAIL_COND_V_MSG(
        !FileAccess::exists(p_from),
        Error::ERR_FILE_NOT_FOUND,
        vformat("'%s' does not exist.", p_from)
    );

    DirAccess *dir;
    Error import_dir_error = _create_import_dir(&dir);
    ERR_FAIL_COND_V_MSG(
        !(import_dir_error == Error::OK || import_dir_error == Error::ERR_ALREADY_EXISTS),
        import_dir_error,
        "Could not create the .import directory."
    );

    // Check to see if the corresponding .md5 file exists, and if it does, does
    // it contain the same hash? If so, we don't need to copy the file again.
    String file_import_name = p_from.get_file() + "-" + p_from.md5_text();
    String md5_file_path = dir->get_current_dir() + "/" + file_import_name + ".md5";

    String md5 = FileAccess::get_md5(p_from);
    FileAccess *md5_file;

    bool skip = false;

    if (!force && FileAccess::exists(md5_file_path) && FileAccess::exists(p_to)) {
        md5_file = FileAccess::open(md5_file_path, FileAccess::READ);
        ERR_FAIL_COND_V_MSG(
            !md5_file,
            Error::ERR_FILE_CANT_READ,
            vformat("Could not open the file '%s'.", md5_file_path)
        );

        String claimed_md5 = md5_file->get_line();

        md5_file->close();
        memdelete(md5_file);

        if (claimed_md5 == md5) {
            skip = true;
        }
    }

    memdelete(dir);

    if (skip) {
        return Error::ERR_ALREADY_EXISTS;
    }

    // If either the .md5 file doesn't exist, or the hash is not the same, then
    // copy the file over.
    DirAccess *main_dir = DirAccess::create(DirAccess::AccessType::ACCESS_FILESYSTEM);
    Error copy_error = main_dir->copy(p_from, p_to);
    ERR_FAIL_COND_V_MSG(
        copy_error != Error::OK,
        copy_error,
        vformat("Could not copy from '%s' to '%s'.", p_from, p_to)
    );

    memdelete(main_dir);

    // Finally, create the .md5 file, and store the hash of the file in it.
    md5_file = FileAccess::open(md5_file_path, FileAccess::WRITE);
    ERR_FAIL_COND_V_MSG(
        !md5_file,
        Error::ERR_FILE_CANT_WRITE,
        vformat("Could not write to '%s'.", md5_file_path)
    );

    md5_file->store_line(md5);
    md5_file->close();

    memdelete(md5_file);

    return Error::OK;
}

Error TabletopImporter::import(const String &p_path, Dictionary options) {
    ERR_FAIL_COND_V_MSG(
        !ResourceFormatImporter::get_singleton()->can_be_imported(p_path),
        Error::ERR_FILE_UNRECOGNIZED,
        vformat("Cannot import '%s', unknown file format.", p_path)
    );

    Ref<ResourceImporter> importer = ResourceFormatImporter::get_singleton()->get_importer_by_extension(p_path.get_extension());
    return _import_resource(importer, p_path, options);
}

void TabletopImporter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("copy_file", "from", "to", "force"), &TabletopImporter::copy_file, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("import", "path", "options"), &TabletopImporter::import, DEFVAL(Dictionary()));
}

Error TabletopImporter::_create_import_dir(DirAccess **dir) {
    Error dir_error = Error::OK;
    DirAccess *import_dir = DirAccess::open(OS::get_singleton()->get_user_data_dir(), &dir_error);
    ERR_FAIL_COND_V_MSG(
        dir_error != Error::OK,
        dir_error,
        "Failed to open the user:// directory."
    );

    dir_error = import_dir->make_dir(".import");

    if (dir) {
        import_dir->change_dir(".import");
        *dir = import_dir;
    } else {
        memdelete(import_dir);
    }

    return dir_error;
}

Error TabletopImporter::_import_resource(Ref<ResourceImporter> p_importer, const String &p_path, Dictionary options) {

    ERR_FAIL_COND_V_MSG(
        !FileAccess::exists(p_path),
        Error::ERR_FILE_NOT_FOUND,
        vformat("'%s' does not exist.", p_path)
    );

    /**
     * STEP 1: Make sure the directories we want to write to exist.
     *
     * user://.import for the .stex and .md5 files.
     */
    DirAccess *dir;
    Error import_dir_error = _create_import_dir(&dir);
    ERR_FAIL_COND_V_MSG(
        !(import_dir_error == Error::OK || import_dir_error == Error::ERR_ALREADY_EXISTS),
        import_dir_error,
        "Could not create the .import directory."
    );

    /**
     * STEP 2: Use the importer object to create a .stex file in the .import
     * folder.
     *
     * This point onwards is based from the code in:
     * editor/editor_file_system.cpp EditorFileSystem::_reimport_file
     */

    // Get the default parameters.
    Map<StringName, Variant> params;

    List<ResourceImporter::ImportOption> opts;
    p_importer->get_import_options(&opts, ResourceImporterTexture::PRESET_3D);

    for (List<ResourceImporter::ImportOption>::Element *E = opts.front(); E; E = E->next())
    {
        String name = E->get().option.name;
        if (options.has(name))
        {
            params[name] = options.get(name, E->get().default_value);
        }
        else
        {
            params[name] = E->get().default_value;
        }
    }

    // The location where the .stex file will be located.
    String file_import_path = dir->get_current_dir() + "/" + p_path.get_file() + "-" + p_path.md5_text();
    memdelete(dir);

    List<String> import_variants;
    Error import_error = p_importer->import(p_path, file_import_path, params, &import_variants);
    ERR_FAIL_COND_V_MSG(
        import_error != Error::OK,
        import_error,
        vformat("Failed to import the file at '%s'.", p_path)
    );

    /**
     * STEP 3: Create a .import file next to the resource file so Godot knows
     * how to load it.
     */

    FileAccess *file = FileAccess::open(p_path + ".import", FileAccess::WRITE);
    ERR_FAIL_COND_V_MSG(
        !file,
        Error::ERR_FILE_CANT_WRITE,
        vformat("Could not open the file at '%s'.", p_path + ".import")
    );

    file->store_line("[remap]");
    file->store_line("importer=\"" + p_importer->get_importer_name() + "\"");
    if (p_importer->get_resource_type() != "") {
        file->store_line("type=\"" + p_importer->get_resource_type() + "\"");
    }

    if (p_importer->get_save_extension() == "") {
        // No path.
    } else if (import_variants.size()) {
        // Import with variants.
        for (List<String>::Element *E = import_variants.front(); E; E = E->next()) {
            String path = file_import_path.c_escape() + "." + E->get() + "." + p_importer->get_save_extension();
            file->store_line("path." + E->get() + "=\"" + path + "\"");
        }
    } else {
        String path = file_import_path + "." + p_importer->get_save_extension();
        file->store_line("path=\"" + path + "\"");
    }

    // Store params
    file->store_line("[params]");
    for (List<ResourceImporter::ImportOption>::Element *E = opts.front(); E; E = E->next()) {
        file->store_line( E->get().option.name + "=" + params[E->get().option.name] );
    }

    file->close();
    memdelete(file);

    return Error::OK;
}
