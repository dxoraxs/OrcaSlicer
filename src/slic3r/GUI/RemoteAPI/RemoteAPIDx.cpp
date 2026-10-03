// dxoraxs fork: Remote API handlers for projects (new/open) and plates.
// Kept apart from RemoteAPIController.cpp so upstream merges rarely conflict; the controller
// only declares these members and routes to them.
//
// None of these may open a modal dialog: a modal blocks the GUI loop, and every later API call
// then times out. So anything that would ask "save changes?" is refused with 409
// unsaved_changes unless the caller passes "discard": true.
#include "RemoteAPIController.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Tab.hpp"

#include <boost/filesystem.hpp>

#include <algorithm>
#include <cctype>

namespace Slic3r { namespace GUI { namespace RemoteAPI {

namespace {

int object_index(const Model &model, uint64_t id)
{
    for (size_t i = 0; i < model.objects.size(); ++i)
        if ((uint64_t) model.objects[i]->id().id == id)
            return (int) i;
    return -1;
}

bool has_unsaved_changes(Plater *plater)
{
    return !plater->up_to_date(false, false) || wxGetApp().has_current_preset_changes();
}

// The Discard branch of GUI_App::check_and_save_current_preset_changes, without the dialog.
void discard_preset_changes()
{
    GUI_App &app = wxGetApp();
    PrinterTechnology tech = app.preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab *tab : app.tabs_list)
        if (tab->supports_printer_technology(tech) && tab->current_preset_is_dirty())
            tab->get_presets()->discard_current_changes();
    app.load_current_presets(false);
}

nlohmann::json plates_json(Plater *plater)
{
    PartPlateList &plates = plater->get_partplate_list();
    nlohmann::json list = nlohmann::json::array();
    for (int i = 0; i < plates.get_plate_count(); ++i) {
        PartPlate *plate = plates.get_plate(i);
        nlohmann::json objs = nlohmann::json::array();
        for (ModelObject *mo : plate->get_objects_on_this_plate())
            objs.push_back({{"id", (uint64_t) mo->id().id}, {"name", mo->name}});
        list.push_back({{"index", i},
                        {"name", plate->get_plate_name()},
                        {"objects", objs},
                        {"slice_result_valid", plate->is_slice_result_valid()}});
    }
    return {{"current", plates.get_curr_plate_index()}, {"plates", list}};
}

nlohmann::json project_json(Plater *plater)
{
    return {{"project", plater->get_project_filename().ToUTF8().data()},
            {"objects", (unsigned) plater->model().objects.size()},
            {"plates", plater->get_partplate_list().get_plate_count()}};
}

bool read_discard(const nlohmann::json &in, bool &discard)
{
    if (in.contains("discard") && !in["discard"].is_boolean())
        return false;
    discard = in.value("discard", false);
    return true;
}

int read_index(const nlohmann::json &in)
{
    if (!in.contains("index") || !in["index"].is_number_integer())
        return -1;
    return in["index"].get<int>();
}

} // namespace

// POST /api/v1/project/new  {"discard": false}
Response Controller::handle_project_new(const std::string &body)
{
    nlohmann::json in = body.empty() ? nlohmann::json::object() : nlohmann::json::parse(body);
    bool discard = false;
    if (!in.is_object() || !read_discard(in, discard))
        return { 400, {{"error", "invalid_body"}} };
    nlohmann::json r = run_on_ui([discard]() -> nlohmann::json {
        Plater *plater = wxGetApp().plater();
        if (has_unsaved_changes(plater) && !discard)
            return {{"error", "unsaved_changes"}};
        if (discard)
            discard_preset_changes();
        plater->new_project(/*skip_confirm=*/true, /*silent=*/true);
        return project_json(plater);
    }, /*timeout_s=*/60);
    if (r.contains("error")) return { 409, r };
    return { 200, r };
}

// POST /api/v1/project/open  {"path": "/abs/name.3mf", "discard": false}
// Loads a .3mf as a project (models, plates, settings), like File > Open Project.
Response Controller::handle_project_open(const std::string &body)
{
    nlohmann::json in = nlohmann::json::parse(body);
    bool discard = false;
    if (!in.is_object() || !read_discard(in, discard))
        return { 400, {{"error", "invalid_body"}} };
    if (!in.contains("path") || !in["path"].is_string())
        return { 400, {{"error", "missing_path"}} };
    std::string path = in["path"].get<std::string>();
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return (char) std::tolower(c); });
    if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".3mf") != 0)
        return { 422, {{"error", "bad_extension"}, {"detail", "projects are .3mf"}} };

    nlohmann::json r = run_on_ui([path, discard]() -> nlohmann::json {
        wxString filename = wxString::FromUTF8(path.c_str());
        boost::system::error_code ec;
        if (!boost::filesystem::exists(into_path(filename), ec))
            return {{"error", "not_found"}};
        Plater *plater = wxGetApp().plater();
        if (has_unsaved_changes(plater)) {
            if (!discard)
                return {{"error", "unsaved_changes"}};
            discard_preset_changes();
            // A clean empty project first: load_project then finds nothing to confirm.
            plater->new_project(/*skip_confirm=*/true, /*silent=*/true);
        }
        // "<loadall>" skips the "open as project or import geometry" question.
        plater->load_project(filename, "<loadall>");
        if (plater->model().objects.empty())
            return {{"error", "load_failed"}};
        return project_json(plater);
    }, /*timeout_s=*/120);
    if (r.contains("error")) {
        const std::string err = r["error"].get<std::string>();
        if (err == "not_found") return { 404, r };
        if (err == "unsaved_changes") return { 409, r };
        return { 422, r };
    }
    return { 200, r };
}

// GET /api/v1/plates
Response Controller::handle_get_plates()
{
    nlohmann::json r = run_on_ui([]() -> nlohmann::json { return plates_json(wxGetApp().plater()); });
    return { 200, r };
}

// POST /api/v1/plates  {"name": "optional"}: add a plate and make it current
Response Controller::handle_add_plate(const std::string &body)
{
    nlohmann::json in = body.empty() ? nlohmann::json::object() : nlohmann::json::parse(body);
    if (!in.is_object() || (in.contains("name") && !in["name"].is_string()))
        return { 400, {{"error", "invalid_body"}} };
    std::string name = in.value("name", std::string());
    nlohmann::json r = run_on_ui([name]() -> nlohmann::json {
        Plater *plater = wxGetApp().plater();
        PartPlateList &plates = plater->get_partplate_list();
        plater->take_snapshot("add partplate");
        int idx = plates.create_plate();
        if (idx < 0)
            return {{"error", "create_failed"}};
        if (!name.empty())
            plates.get_plate(idx)->set_plate_name(name);
        plater->select_plate(idx);
        plater->update();
        nlohmann::json out = plates_json(plater);
        out["added"] = idx;
        return out;
    });
    if (r.contains("error")) return { 422, r };
    return { 200, r };
}

// POST /api/v1/plates/select  {"index": 1}
Response Controller::handle_select_plate(const std::string &body)
{
    nlohmann::json in = nlohmann::json::parse(body);
    int index = in.is_object() ? read_index(in) : -1;
    if (index < 0)
        return { 400, {{"error", "invalid_index"}} };
    nlohmann::json r = run_on_ui([index]() -> nlohmann::json {
        Plater *plater = wxGetApp().plater();
        if (index >= plater->get_partplate_list().get_plate_count())
            return {{"error", "unknown_plate"}};
        if (plater->select_plate(index) != 0)
            return {{"error", "select_failed"}};
        return plates_json(plater);
    });
    if (r.contains("error")) return { r["error"] == "unknown_plate" ? 404 : 422, r };
    return { 200, r };
}

// DELETE /api/v1/plates/{index}: only an empty plate, never the last one
Response Controller::handle_delete_plate(int index)
{
    nlohmann::json r = run_on_ui([index]() -> nlohmann::json {
        Plater *plater = wxGetApp().plater();
        PartPlateList &plates = plater->get_partplate_list();
        if (index < 0 || index >= plates.get_plate_count())
            return {{"error", "unknown_plate"}};
        if (plates.get_plate_count() == 1)
            return {{"error", "last_plate"}};
        if (!plates.get_plate(index)->get_objects_on_this_plate().empty())
            return {{"error", "plate_not_empty"}};
        if (plater->delete_plate(index) != 0)
            return {{"error", "delete_failed"}};
        return plates_json(plater);
    });
    if (r.contains("error")) {
        const std::string err = r["error"].get<std::string>();
        return { err == "unknown_plate" ? 404 : 409, r };
    }
    return { 200, r };
}

// POST /api/v1/objects/{id}/plate  {"index": 1}: move the object (all its instances) onto a
// plate, keeping its position relative to the plate.
Response Controller::handle_move_object_to_plate(uint64_t id, const std::string &body)
{
    nlohmann::json in = nlohmann::json::parse(body);
    int index = in.is_object() ? read_index(in) : -1;
    if (index < 0)
        return { 400, {{"error", "invalid_index"}} };
    nlohmann::json r = run_on_ui([id, index]() -> nlohmann::json {
        Plater *plater = wxGetApp().plater();
        PartPlateList &plates = plater->get_partplate_list();
        int idx = object_index(plater->model(), id);
        if (idx < 0)
            return {{"error", "unknown_object"}};
        if (index >= plates.get_plate_count())
            return {{"error", "unknown_plate"}};
        ModelObject *mo = plater->model().objects[idx];
        if (mo->instances.empty())
            return {{"error", "no_instance"}};
        int from = plates.find_instance_belongs(idx, 0);
        if (from < 0)
            from = plates.get_curr_plate_index();
        if (from != index) {
            Vec3d delta = plates.get_plate(index)->get_origin() - plates.get_plate(from)->get_origin();
            delta.z() = 0.0;
            plater->take_snapshot("move object to plate");
            for (size_t i = 0; i < mo->instances.size(); ++i) {
                ModelInstance *mi = mo->instances[i];
                mi->set_offset(mi->get_offset() + delta);
                plates.notify_instance_update(idx, (int) i);
            }
            plater->changed_object(idx);
        }
        return {{"id", id}, {"plate", plates.find_instance_belongs(idx, 0)}};
    });
    if (r.contains("error")) {
        const std::string err = r["error"].get<std::string>();
        return { (err == "unknown_object" || err == "unknown_plate") ? 404 : 422, r };
    }
    return { 200, r };
}

}}} // namespace
