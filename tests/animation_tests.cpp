#define GLFW_INCLUDE_NONE
#include <glad.h>
#include "animation_editor.hpp"
#include "entity_clipboard.hpp"
#include "animation_view.hpp"
#include "recent_projects.hpp"
#include "project_persistence_system.hpp"
#include "components/camera.hpp"
#include "components/transform.hpp"
#include "components/sound_player.hpp"
#include <gtest/gtest.h>
#include <os/asset_manager.hpp>
#include <chrono>

namespace {
struct AnimationTest : testing::Test {
    std::shared_ptr<commons::ecs> registry = std::make_shared<commons::ecs>();
    std::shared_ptr<editor::ui::editor_context> context = std::make_shared<editor::ui::editor_context>(registry);
    commons::entity target = registry->create();
    editor::ui::animation_editor editor{context};
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("bengine-animation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override { std::filesystem::create_directories(root); context->selected_entity = target.id; }
    void TearDown() override { std::filesystem::remove_all(root); }
    commons::animation_clip clip() {
        commons::animation_clip result;
        result.tracks.push_back({commons::transform::mask, "position.x", false, {{0,0},{2,4}}});
        return result;
    }
};
}
TEST_F(AnimationTest, SamplesBoundariesLinearAndSteppedAndReplacesDuplicateTime) {
    auto track = clip().tracks.front();
    EXPECT_DOUBLE_EQ(*track.sample(-1), 0);
    EXPECT_DOUBLE_EQ(*track.sample(1), 2);
    EXPECT_DOUBLE_EQ(*track.sample(3), 4);
    track.stepped = true;
    EXPECT_DOUBLE_EQ(*track.sample(1), 0);
    track.insert({2,8});
    EXPECT_EQ(track.keys.size(), 2u);
    EXPECT_DOUBLE_EQ(*track.sample(2), 8);
    EXPECT_THROW(track.insert({-1,3}), std::invalid_argument);
    EXPECT_FALSE(track.sample(std::numeric_limits<double>::quiet_NaN()));
}
TEST_F(AnimationTest, RejectsMalformedAnimationWithoutMutatingPreviousData) {
    auto previous = clip();
    rapidjson::Document document(rapidjson::kObjectType);
    ASSERT_TRUE(previous.serialize(document, document.GetAllocator()));
    document["tracks"][0]["keys"][1][0].SetDouble(-1);
    EXPECT_FALSE(previous.analyze(document));
    EXPECT_DOUBLE_EQ(previous.tracks.front().keys.back().time, 2);
    document["tracks"][0]["keys"][1][0].SetDouble(0);
    EXPECT_FALSE(previous.analyze(document));
}
TEST_F(AnimationTest, SupportsExtensibleComponentChannels) {
    struct custom : commons::component {
        double value = 2;
        std::vector<commons::animation_property> animation_properties() const override { return {{"custom",0,10,false}}; }
        std::optional<double> read_animation_property(const std::string& p) const override { return p == "custom" ? std::optional<double>(value) : std::nullopt; }
        bool write_animation_property(const std::string& p, double n) override { if (p != "custom" || n < 0 || n > 10) return false; value = n; return true; }
    } value;
    ASSERT_EQ(commons::animation_properties(value).size(),1u);
    EXPECT_TRUE(commons::write_animation_property(value,"custom",4));
    EXPECT_DOUBLE_EQ(*commons::read_animation_property(value,"custom"),4);
}
TEST_F(AnimationTest, PreviewIsUndoableAndIgnoresRemovedOrReplacedComponents) {
    ASSERT_TRUE(editor.commit(target.id,clip()));
    ASSERT_TRUE(editor.preview(target.id,1));
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x,2);
    ASSERT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x,0);
    ASSERT_TRUE(context->redo());
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x,2);
    registry->add<commons::transform>(target);
    ASSERT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x,0);
    registry->remove<commons::transform>(target.id);
    EXPECT_FALSE(editor.preview(target.id,1));
}
TEST_F(AnimationTest, KeyframeCreationUndoRedoAndEntityReuseAreSafe) {
    ASSERT_TRUE(editor.commit(target.id,clip()));
    ASSERT_EQ(registry->get<commons::animation>(target.id)->my_object, target.id);
    ASSERT_TRUE(context->undo());
    EXPECT_FALSE(editor.clip(target.id));
    ASSERT_TRUE(context->redo());
    ASSERT_TRUE(editor.clip(target.id));
    ASSERT_TRUE(context->undo());
    registry->remove(target.id); auto replacement = registry->create(target.id);
    ASSERT_TRUE(context->redo());
    EXPECT_FALSE(editor.clip(replacement.id));
}
TEST_F(AnimationTest, RuntimeLoopsAndIgnoresMissingTargets) {
    ASSERT_TRUE(editor.commit(target.id,clip()));
    double dt=1.; commons::animation_system system([&]{ return dt; });
    system.update(registry); system.update(registry);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x,2);
    dt=4.; system.update(registry);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x,4);
    dt=0.; system.update(registry);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x,2);
    registry->remove<commons::transform>(target.id);
    EXPECT_NO_THROW(system.update(registry));
    registry->remove(target.id);
    EXPECT_NO_THROW(system.update(registry));
}
TEST_F(AnimationTest, SoundChannelsValidateRangesAndBooleanState) {
    commons::sound_player sound;
    EXPECT_FALSE(commons::write_animation_property(sound,"volume",2));
    EXPECT_TRUE(commons::write_animation_property(sound,"volume",.25));
    EXPECT_FLOAT_EQ(sound.volume,.25);
    EXPECT_TRUE(commons::write_animation_property(sound,"looping",1));
    EXPECT_TRUE(sound.looping);
    EXPECT_FALSE(commons::write_animation_property(sound,"pitch",std::numeric_limits<double>::infinity()));
}
TEST_F(AnimationTest, ScenePersistenceRoundTripsKeyframes) {
    auto camera = registry->create(); registry->add<commons::camera>(camera);
    ASSERT_TRUE(editor.commit(target.id,clip()));
    editor::ui::project_persistence_system persistence(registry,camera.id);
    std::string error; const auto path = root / "project.bengine";
    ASSERT_TRUE(persistence.save(path.string(),error)) << error;
    registry->remove<commons::animation>(target.id);
    ASSERT_TRUE(persistence.load(path.string(),error)) << error;
    auto loaded = registry->get<commons::animation>(target.id);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->data->tracks,clip().tracks);
}
TEST_F(AnimationTest, RecentProjectsUsesLastSuccessAndSkipsMissingPaths) {
    editor::ui::recent_projects recent(root/"recent.cfg"); std::string error;
    const auto first=root/"first.bengine", second=root/"second.bengine";
    std::ofstream(first)<<"{}"; std::ofstream(second)<<"{}";
    ASSERT_TRUE(recent.remember(first.string(),error)) << error;
    ASSERT_TRUE(recent.remember(second.string(),error)) << error;
    ASSERT_EQ(recent.last(),second.string());
    std::filesystem::remove(second);
    ASSERT_EQ(recent.last(),first.string());
}
TEST_F(AnimationTest, AnimationPanelSurvivesClosingAndReopening) {
    bgui::scoped_interface interface;
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::animation_view panel(context); panel.build(dock); panel.update();
    bgui::window* window = nullptr;
    for (auto& [layer, elements] : dock.get_elements()) for (auto& e : elements)
        if (auto* w=dynamic_cast<bgui::window*>(e.get()); w && w->get_title().get_buffer()==editor::ui::tr("Animação")) window=w;
    ASSERT_NE(window,nullptr);
    ASSERT_TRUE(dock.remove_window(window));
    EXPECT_NO_THROW(panel.update());
    EXPECT_NO_THROW(panel.show());
    EXPECT_NO_THROW(panel.update());
}

TEST_F(AnimationTest, ClipboardPreservesAnimationTracks) {
    ASSERT_TRUE(editor.commit(target.id,clip()));
    editor::ui::entity_clipboard clipboard;
    std::string error;
    ASSERT_TRUE(clipboard.copy(context,error)) << error;
    ASSERT_TRUE(clipboard.paste(context,error)) << error;
    ASSERT_TRUE(context->selected_entity);
    EXPECT_NE(*context->selected_entity,target.id);
    auto copied = registry->get<commons::animation>(*context->selected_entity);
    ASSERT_TRUE(copied);
    EXPECT_EQ(copied->data->tracks,clip().tracks);
}

TEST_F(AnimationTest, TimelineRulerLeavesRoomForKeyframeEditors) {
    bgui::scoped_interface interface;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    ASSERT_TRUE(editor.commit(target.id,clip()));
    editor::ui::animation_view panel(context);
    panel.build(dock);
    panel.update();
    dock.compute_style();
    dock.process_required_size({1200,800});
    dock.set_final_rect(0,0,1200,800);
    dock.cascade_style();
    dock.on_update();
    bgui::element::begin_arranging();
    dock.arrange();
    bgui::element::end_arranging();
    bgui::text* start = nullptr;
    std::function<void(bgui::layout&)> find = [&](bgui::layout& parent) {
        for (auto& [layer, elements] : parent.get_elements()) for (auto& value : elements) {
            if (auto* label = dynamic_cast<bgui::text*>(value.get()); label && label->get_buffer() == "0 s") start=label;
            if (auto* layout=value->as_layout()) find(*layout);
        }
    };
    find(dock);
    ASSERT_NE(start,nullptr);
    EXPECT_GT(start->get_parent()->processed_height(),0);
    EXPECT_LT(start->get_parent()->processed_height(),32);
}

TEST_F(AnimationTest, LuaChannelsUpdateLivePropertiesWithoutReloadingScript) {
    commons::code script;
    script.file="movement.lua";
    script.properties.push_back({"speed","number",2.0});
    script.estado_lua["properties"] = script.estado_lua.create_table();
    ASSERT_TRUE(commons::write_animation_property(script,"script[movement.lua].speed",5));
    EXPECT_DOUBLE_EQ(std::get<double>(script.properties.front().value),5);
    sol::table table=script.estado_lua["properties"];
    EXPECT_DOUBLE_EQ(table["speed"].get<double>(),5);
}

TEST_F(AnimationTest, HudChannelsFollowStableIdsWhenElementsAreReordered) {
    commons::hud hud;
    auto first=hud.add("text","First");
    auto second=hud.add("text","Second");
    first->id="first"; second->id="second";
    std::swap(hud.elements[0],hud.elements[1]);
    ASSERT_TRUE(commons::write_animation_property(hud,"elements[first].width",120));
    EXPECT_FLOAT_EQ(first->width,120);
    EXPECT_FLOAT_EQ(second->width,0);
}

TEST_F(AnimationTest, CreatesExternalClipAndSceneContainsOnlyPlayerReference) {
    context->project_root=root;
    std::string error;
    ASSERT_TRUE(editor.create(target.id,"Walk",error)) << error;
    auto player=editor.player(target.id);
    ASSERT_TRUE(player);
    EXPECT_EQ(player->clip,"assets/animations/Walk.banim");
    ASSERT_TRUE(std::filesystem::is_regular_file(root/player->clip));
    auto next=*editor.clip(target.id); next.tracks=clip().tracks;
    ASSERT_TRUE(editor.commit(target.id,next));
    ASSERT_TRUE(editor.save(target.id,error)) << error;
    rapidjson::Document component(rapidjson::kObjectType);
    ASSERT_TRUE(player->serialize(component,component.GetAllocator()));
    EXPECT_TRUE(component.HasMember("clip"));
    EXPECT_FALSE(component.HasMember("tracks"));
    auto loaded=commons::load_animation_clip(root/player->clip);
    EXPECT_EQ(loaded->tracks,next.tracks);
}

TEST_F(AnimationTest, SharedClipEditsAreVisibleToAllAssignedEntities) {
    context->project_root=root;
    std::string error;
    ASSERT_TRUE(editor.create(target.id,"Shared",error)) << error;
    auto other=registry->create();
    ASSERT_TRUE(editor.assign(other.id,"assets/animations/Shared.banim",error)) << error;
    EXPECT_EQ(editor.clip(other.id),editor.clip(target.id));
    auto next=*editor.clip(target.id); next.tracks=clip().tracks;
    ASSERT_TRUE(editor.commit(target.id,next));
    EXPECT_EQ(editor.clip(other.id)->tracks,next.tracks);
    ASSERT_TRUE(context->undo());
    EXPECT_TRUE(editor.clip(other.id)->tracks.empty());
}

TEST_F(AnimationTest, RecordingAutomaticallyCreatesTracksForChangedProperties) {
    context->project_root=root;
    std::string error;
    ASSERT_TRUE(editor.create(target.id,"Recorded",error)) << error;
    editor.begin_recording(target.id);
    auto transform=registry->get<commons::transform>(target.id);
    transform->position.x=3; transform->rotation.y=90;
    ASSERT_TRUE(editor.record_changes(target.id,2));
    auto data=editor.clip(target.id);
    ASSERT_EQ(data->tracks.size(),2u);
    for(const auto& track:data->tracks) {
        ASSERT_EQ(track.keys.size(),2u);
        EXPECT_DOUBLE_EQ(track.keys[0].time,0);
        EXPECT_DOUBLE_EQ(track.keys[1].time,2);
    }
    EXPECT_FALSE(editor.record_changes(target.id,2));
    transform->position.x=5;
    ASSERT_TRUE(editor.record_changes(target.id,2));
    auto track=std::find_if(data->tracks.begin(),data->tracks.end(),[](const auto& t){return t.property=="position.x";});
    ASSERT_NE(track,data->tracks.end());
    EXPECT_EQ(track->keys.size(),2u);
    EXPECT_DOUBLE_EQ(track->keys.back().value,5);
}

TEST_F(AnimationTest, RecordingIgnoresPreviewAndCombinesInspectorUndo) {
    context->project_root=root;
    std::string error;
    ASSERT_TRUE(editor.create(target.id,"Undo",error)) << error;
    editor.begin_recording(target.id);
    auto transform=registry->get<commons::transform>(target.id);
    transform->position.x=4;
    context->record_action([transform]{transform->position.x=0;},[transform]{transform->position.x=4;});
    ASSERT_TRUE(editor.record_changes(target.id,2));
    ASSERT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(transform->position.x,0);
    EXPECT_TRUE(editor.clip(target.id)->tracks.empty());
    EXPECT_FALSE(editor.record_changes(target.id,2));
    ASSERT_TRUE(context->redo());
    editor.begin_recording(target.id);
    ASSERT_TRUE(editor.preview(target.id,1));
    EXPECT_FALSE(editor.record_changes(target.id,1));
}

TEST_F(AnimationTest, MissingOrMalformedClipAssignmentPreservesExistingPlayer) {
    context->project_root=root;
    std::string error;
    ASSERT_TRUE(editor.create(target.id,"Original",error)) << error;
    const auto original=editor.clip(target.id);
    EXPECT_FALSE(editor.assign(target.id,"assets/animations/missing.banim",error));
    EXPECT_EQ(editor.clip(target.id),original);
    std::ofstream(root/"bad.banim")<<"not json";
    EXPECT_FALSE(editor.assign(target.id,"bad.banim",error));
    EXPECT_EQ(editor.clip(target.id),original);
    EXPECT_FALSE(editor.assign(target.id,"../outside.banim",error));
}

TEST_F(AnimationTest, SaveRejectsExternalEditsAndKeepsOriginalDiskContents) {
    context->project_root=root;
    std::string error;
    ASSERT_TRUE(editor.create(target.id,"Conflict",error)) << error;
    auto next=*editor.clip(target.id); next.tracks=clip().tracks;
    ASSERT_TRUE(editor.commit(target.id,next));
    const auto path=root/editor.player(target.id)->clip;
    std::ofstream(path)<<"external change";
    EXPECT_FALSE(editor.save(target.id,error));
    EXPECT_EQ(editor::ui::project_files::read(path),"external change");
    EXPECT_TRUE(editor.clip(target.id)->dirty());
}

TEST_F(AnimationTest, ProjectSaveKeepsDirtyAssetAfterUnassigningOrDeletingEntity) {
    context->project_root=root;
    std::string error;
    auto camera=registry->create(); registry->add<commons::camera>(camera);
    editor::ui::project_persistence_system persistence(registry,camera.id);
    persistence.set_animation_assets_provider([this]{return context->animation_assets;});
    ASSERT_TRUE(editor.create(target.id,"Retained",error)) << error;
    const auto path=root/editor.player(target.id)->clip;
    auto next=*editor.clip(target.id); next.tracks=clip().tracks;
    ASSERT_TRUE(editor.commit(target.id,next));
    registry->remove(target.id);
    ASSERT_TRUE(persistence.save((root/"project.bengine").string(),error)) << error;
    EXPECT_EQ(commons::load_animation_clip(path)->tracks,next.tracks);
}

TEST_F(AnimationTest, RecorderCreatesConstantBooleanTracksWithoutChoosingAChannel) {
    context->project_root=root;
    registry->add<commons::renderer>(target);
    std::string error;
    ASSERT_TRUE(editor.create(target.id,"Visibility",error)) << error;
    editor.begin_recording(target.id);
    registry->get<commons::renderer>(target.id)->visible=false;
    ASSERT_TRUE(editor.record_changes(target.id,1));
    auto asset=editor.clip(target.id);
    ASSERT_EQ(asset->tracks.size(),1u);
    EXPECT_EQ(asset->tracks.front().property,"visible");
    EXPECT_TRUE(asset->tracks.front().stepped);
    EXPECT_DOUBLE_EQ(*asset->tracks.front().sample(.5),1);
    EXPECT_DOUBLE_EQ(*asset->tracks.front().sample(1),0);
}

TEST_F(AnimationTest, MissingReferencedClipDoesNotReplaceTheCurrentScene) {
    context->project_root=root;
    auto camera=registry->create(); registry->add<commons::camera>(camera);
    editor::ui::project_persistence_system persistence(registry,camera.id);
    std::string error;
    ASSERT_TRUE(editor.create(target.id,"Missing",error)) << error;
    ASSERT_TRUE(persistence.save((root/"project.bengine").string(),error)) << error;
    std::filesystem::remove(root/editor.player(target.id)->clip);
    registry->get<commons::transform>(target.id)->position.x=7;
    EXPECT_FALSE(persistence.load((root/"project.bengine").string(),error));
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x,7);
}

TEST_F(AnimationTest, LegacyInlineAnimationMigratesWithoutKeepingCurvesInTheScene) {
    context->project_root=root;
    auto camera=registry->create(); registry->add<commons::camera>(camera);
    editor::ui::project_persistence_system persistence(registry,camera.id);
    ASSERT_TRUE(editor.commit(target.id,clip()));
    EXPECT_TRUE(editor.player(target.id)->clip.empty());
    std::string error;
    ASSERT_TRUE(persistence.save((root/"project.bengine").string(),error)) << error;
    ASSERT_FALSE(editor.player(target.id)->clip.empty());
    EXPECT_TRUE(std::filesystem::is_regular_file(root/editor.player(target.id)->clip));
    rapidjson::Document project;
    const auto project_text=editor::ui::project_files::read(root/"project.bengine");
    project.Parse(project_text.c_str());
    rapidjson::Document scene;
    const auto scene_text=editor::ui::project_files::read(root/project["scene"].GetString());
    scene.Parse(scene_text.c_str());
    const auto& data=scene["entities"][0]["components"]["animation"];
    EXPECT_TRUE(data.HasMember("clip"));
    EXPECT_FALSE(data.HasMember("tracks"));
    EXPECT_EQ(commons::load_animation_clip(root/data["clip"].GetString())->tracks,clip().tracks);
}

TEST_F(AnimationTest, ReloadingAClipPreservesLocalEditsInUndoHistory) {
    context->project_root=root;
    std::string error;
    ASSERT_TRUE(editor.create(target.id,"Reload",error)) << error;
    auto next=*editor.clip(target.id); next.tracks=clip().tracks;
    ASSERT_TRUE(editor.commit(target.id,next));
    auto disk=commons::animation_clip{}; disk.duration=2;
    std::ofstream(editor.clip(target.id)->source_path)<<disk.encode();
    ASSERT_TRUE(editor.reload(target.id,error)) << error;
    EXPECT_TRUE(editor.clip(target.id)->tracks.empty());
    ASSERT_TRUE(context->undo());
    EXPECT_EQ(editor.clip(target.id)->tracks,next.tracks);
    ASSERT_TRUE(editor.save(target.id,error)) << error;
}

TEST_F(AnimationTest, PanelRecordsInspectorChangesWithoutAPropertyPicker) {
    bgui::scoped_interface interface;
    context->project_root=root;
    std::string error;
    ASSERT_TRUE(editor.create(target.id,"Panel",error)) << error;
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::animation_view panel(context); panel.build(dock); panel.update();
    bgui::button* record=nullptr;
    std::function<void(bgui::layout&)> find=[&](bgui::layout& parent) {
        for(auto& [layer, elements]:parent.get_elements()) for(auto& value:elements) {
            if(auto* button=dynamic_cast<bgui::button*>(value.get()); button && button->get_label().get_buffer()==editor::ui::tr("● Gravar")) record=button;
            if(auto* layout=value->as_layout()) find(*layout);
        }
    };
    find(dock);
    ASSERT_NE(record,nullptr);
    record->on_released(); bgui::on_update();
    registry->get<commons::transform>(target.id)->position.x=9;
    panel.update();
    auto data=editor.clip(target.id);
    ASSERT_EQ(data->tracks.size(),1u);
    EXPECT_EQ(data->tracks.front().property,"position.x");
    EXPECT_DOUBLE_EQ(data->tracks.front().keys.front().value,9);
    context->is_playing=true; panel.update();
    context->is_playing=false; panel.update();
    registry->get<commons::transform>(target.id)->position.x=10;
    panel.update();
    EXPECT_DOUBLE_EQ(data->tracks.front().keys.front().value,9);
}

TEST_F(AnimationTest, ClipPathAndActionsRemainSeparatedAtEditorPanelWidths) {
    bgui::scoped_interface interface;
    auto& styles = bgui::style_manager::get_instance();
    struct restore_theme {
        bgui::theme previous;
        ~restore_theme() { bgui::style_manager::get_instance().apply_theme(previous); }
    } restore{styles.get_theme()};
    styles.apply_theme(bgui::dark_theme());
    bgui::font font;
    font.ascent = 36;
    font.descent = 10;
    for (char32_t c = 32; c < 256; ++c) {
        bgui::character glyph;
        glyph.size = {24, 46};
        glyph.advance = 24;
        font.chs.emplace(c, glyph);
    }
    bgui::asset_manager::get_instance().store_font("animation-layout-test",
        bgui::font_manager::m_default_resolution, std::move(font));
    auto& dock = bgui::get_layout().add<bgui::dock>();
    ASSERT_TRUE(editor.commit(target.id, clip()));
    editor::ui::animation_view panel(context);
    panel.build(dock);
    panel.update();
    bgui::field* path = nullptr;
    std::vector<bgui::button*> actions;
    std::function<void(bgui::layout&)> find = [&](bgui::layout& parent) {
        for (auto& [layer, elements] : parent.get_elements()) for (auto& value : elements) {
            if (auto* label = dynamic_cast<bgui::text*>(value.get()))
                label->style.visual.font = "animation-layout-test";
            if (auto* field = dynamic_cast<bgui::field*>(value.get()); field && field->get_label().get_buffer() == editor::ui::tr("Clipe"))
                path = field;
            if (auto* button = dynamic_cast<bgui::button*>(value.get()); button &&
                (button->get_label().get_buffer() == editor::ui::tr("Atribuir .banim") ||
                 button->get_label().get_buffer() == editor::ui::tr("Salvar clipe") ||
                 button->get_label().get_buffer() == editor::ui::tr("Recarregar"))) actions.push_back(button);
            if (auto* layout = value->as_layout()) find(*layout);
        }
    };
    find(dock);
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(actions.size(), 3);
    for (int width : {680, 900}) {
        for (int frame = 0; frame < 3; ++frame) {
            dock.compute_style();
            dock.process_required_size({width, 800});
            dock.set_final_rect(0, 0, width, 800);
            dock.cascade_style();
            dock.on_update();
            dock.calculate_content_sizes();
            bgui::element::begin_arranging();
            dock.arrange();
            bgui::element::end_arranging();
        }
        EXPECT_GT(path->get_inputbox().processed_width(), 400);
        for (auto* action : actions) {
            EXPECT_GE(action->processed_y(), path->processed_y() + path->processed_height());
            EXPECT_EQ(action->processed_height(), 28);
            EXPECT_GT(action->processed_width(), action->get_label().processed_width());
            EXPECT_LE(action->processed_x() + action->processed_width(), width);
        }
        for (std::size_t i = 1; i < actions.size(); ++i)
            EXPECT_GE(actions[i]->processed_x(), actions[i-1]->processed_x() + actions[i-1]->processed_width() + 6);
    }
}

TEST_F(AnimationTest, ContinuousPreviewGroupsHistoryAndStopsExtendingAfterUndo) {
    ASSERT_TRUE(editor.commit(target.id, clip()));
    registry->get<commons::transform>(target.id)->position.x = 7;
    const auto count = context->undo_stack.size();
    ASSERT_TRUE(editor.preview(target.id, 0, true));
    ASSERT_TRUE(editor.preview(target.id, 1, true));
    ASSERT_TRUE(editor.preview(target.id, 2, true));
    EXPECT_EQ(context->undo_stack.size(), count + 1);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 4);
    ASSERT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 7);
    ASSERT_TRUE(context->redo());
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 4);
    editor.end_preview();
    ASSERT_TRUE(editor.preview(target.id, 1, true));
    EXPECT_EQ(context->undo_stack.size(), count + 2);
    EXPECT_EQ(editor.clip(target.id)->tracks, clip().tracks);
}

TEST_F(AnimationTest, PanelPreviewPlaysPausesLoopsStopsAndDoesNotRecordKeys) {
    ASSERT_TRUE(editor.commit(target.id, clip()));
    auto data = editor.clip(target.id);
    data->duration = 2;
    data->loop = false;
    editor.player(target.id)->speed = 2;
    bgui::scoped_interface interface;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::animation_view::clock::time_point now{};
    editor::ui::animation_view panel(context, [&] { return now; });
    panel.build(dock); panel.update();
    auto click = [&](const std::string& label) {
        bgui::button* found = nullptr;
        std::function<void(bgui::layout&)> find = [&](bgui::layout& parent) {
            for (auto& [layer, elements] : parent.get_elements()) for (auto& value : elements) {
                if (auto* button = dynamic_cast<bgui::button*>(value.get()); button && button->get_label().get_buffer() == label) found = button;
                if (auto* layout = value->as_layout()) find(*layout);
            }
        };
        find(dock);
        if (!found) return false;
        found->on_released(); bgui::on_update(); panel.update();
        return true;
    };
    auto tick = [&](int milliseconds) { now += std::chrono::milliseconds(milliseconds); panel.update(); };
    ASSERT_TRUE(click(editor::ui::tr("● Gravar")));
    const auto history = context->undo_stack.size();
    ASSERT_TRUE(click(editor::ui::tr("Reproduzir prévia")));
    tick(250);
    EXPECT_DOUBLE_EQ(panel.time(), .5);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 1);
    tick(250);
    EXPECT_EQ(context->undo_stack.size(), history + 1);
    ASSERT_TRUE(click(editor::ui::tr("Pausar prévia")));
    tick(500);
    EXPECT_DOUBLE_EQ(panel.time(), 1);
    ASSERT_TRUE(click(editor::ui::tr("Reproduzir prévia")));
    tick(1000);
    EXPECT_DOUBLE_EQ(panel.time(), 2);
    tick(500);
    EXPECT_DOUBLE_EQ(panel.time(), 2);
    ASSERT_TRUE(click(editor::ui::tr("Reproduzir prévia")));
    EXPECT_DOUBLE_EQ(panel.time(), 0);
    data->loop = true;
    tick(1250);
    EXPECT_DOUBLE_EQ(panel.time(), .5);
    ASSERT_TRUE(click(editor::ui::tr("Parar prévia")));
    EXPECT_DOUBLE_EQ(panel.time(), 0);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 0);
    EXPECT_EQ(data->tracks, clip().tracks);
    ASSERT_TRUE(click(editor::ui::tr("Reproduzir prévia")));
    tick(250);
    ASSERT_TRUE(context->undo());
    panel.update(); tick(500);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 0);
    ASSERT_TRUE(click(editor::ui::tr("Reproduzir prévia")));
    editor.player(target.id)->speed = 0;
    const auto frozen = panel.time();
    tick(500);
    EXPECT_DOUBLE_EQ(panel.time(), frozen);
    editor.player(target.id)->speed = 2;
    context->is_playing = true; panel.update();
    context->is_playing = false; panel.update(); tick(500);
    EXPECT_DOUBLE_EQ(panel.time(), frozen);
    ASSERT_TRUE(click(editor::ui::tr("Reproduzir prévia")));
    context->locked_entities.insert(target.id); panel.update();
    context->locked_entities.clear(); context->select_entity(target.id); panel.update(); tick(500);
    EXPECT_DOUBLE_EQ(panel.time(), frozen);
    ASSERT_TRUE(click(editor::ui::tr("Reproduzir prévia")));
    context->selected_entity = registry->create().id; panel.update(); tick(500);
    EXPECT_DOUBLE_EQ(panel.time(), frozen);
    context->selected_entity = target.id; panel.update();
    ASSERT_TRUE(click(editor::ui::tr("Reproduzir prévia")));
    registry->remove<commons::animation>(target.id);
    EXPECT_NO_THROW(panel.update());
    EXPECT_NO_THROW(tick(500));
}

TEST_F(AnimationTest, ContinuousPreviewValidatesTimeAndComponentIdentity) {
    ASSERT_TRUE(editor.commit(target.id, clip()));
    ASSERT_TRUE(editor.preview(target.id, 1, true));
    const auto history = context->undo_stack.size();
    EXPECT_FALSE(editor.preview(target.id, std::numeric_limits<double>::quiet_NaN(), true));
    EXPECT_FALSE(editor.preview(target.id, -1, true));
    EXPECT_FALSE(editor.preview(target.id, 6, true));
    EXPECT_EQ(context->undo_stack.size(), history);
    registry->add<commons::transform>(target);
    auto replacement = registry->get<commons::transform>(target.id);
    replacement->position.x = 8;
    ASSERT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(replacement->position.x, 8);
    ASSERT_TRUE(context->redo());
    EXPECT_FLOAT_EQ(replacement->position.x, 8);
    registry->remove<commons::transform>(target.id);
    EXPECT_FALSE(editor.preview(target.id, 2, true));
}

TEST_F(AnimationTest, MovingKeysSortsWithoutLosingValuesAndSupportsUndo) {
    ASSERT_TRUE(editor.commit(target.id, clip()));
    auto asset = editor.clip(target.id);
    const auto original = asset->tracks;
    const auto history = context->undo_stack.size();
    EXPECT_FALSE(editor.move_key(target.id, 0, 0, 2));
    EXPECT_FALSE(editor.move_key(target.id, 0, 0, -1));
    EXPECT_FALSE(editor.move_key(target.id, 0, 0, std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(editor.move_key(target.id, 1, 0, 1));
    EXPECT_EQ(context->undo_stack.size(), history);
    ASSERT_TRUE(editor.move_key(target.id, 0, 0, 3));
    EXPECT_EQ(asset->tracks.front().keys, (std::vector<commons::animation_key>{{2,4},{3,0}}));
    ASSERT_TRUE(context->undo());
    EXPECT_EQ(asset->tracks, original);
    ASSERT_TRUE(context->redo());
    EXPECT_DOUBLE_EQ(asset->tracks.front().keys.back().time, 3);
}

TEST_F(AnimationTest, TimelineDragCommitsOnceOnRelease) {
    ASSERT_TRUE(editor.commit(target.id, clip()));
    bgui::scoped_interface interface;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::animation_view panel(context); panel.build(dock); panel.update();
    std::vector<bgui::element*> plots;
    std::function<void(bgui::layout&)> find = [&](bgui::layout& parent) {
        for (auto& [layer, elements] : parent.get_elements()) for (auto& value : elements) {
            if (value->type == "animation-timeline") plots.push_back(value.get());
            if (auto* nested = value->as_layout()) find(*nested);
        }
    };
    find(dock);
    ASSERT_EQ(plots.size(), 2u);
    auto* track = plots.back();
    track->set_final_rect(0, 0, 1000, 40);
    const auto history = context->undo_stack.size();
    auto& io = bgui::get_context();
    io.m_mouse_position = {0, 20}; track->on_clicked(); track->on_pressed();
    io.m_mouse_position = {200, 20}; track->set_drag({200, 0});
    panel.update();
    EXPECT_EQ(context->undo_stack.size(), history);
    EXPECT_DOUBLE_EQ(editor.clip(target.id)->tracks.front().keys.front().time, 0);
    track->on_released();
    EXPECT_EQ(context->undo_stack.size(), history + 1);
    EXPECT_DOUBLE_EQ(editor.clip(target.id)->tracks.front().keys.front().time, 1);
    ASSERT_TRUE(context->undo());
    EXPECT_EQ(editor.clip(target.id)->tracks, clip().tracks);
}

TEST_F(AnimationTest, RuntimeCommandsPauseSeekStopAndRespectAutoplay) {
    ASSERT_TRUE(editor.commit(target.id, clip()));
    auto player = editor.player(target.id); player->autoplay = false;
    double dt = 1;
    commons::animation_system system([&] { return dt; });
    system.update(registry);
    EXPECT_FALSE(player->is_playing());
    ASSERT_TRUE(player->play()); system.update(registry); system.update(registry);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 2);
    ASSERT_TRUE(player->pause()); system.update(registry); system.update(registry);
    EXPECT_FALSE(player->is_playing());
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 2);
    ASSERT_TRUE(player->seek(2)); system.update(registry);
    EXPECT_DOUBLE_EQ(player->playback_time(), 2);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 4);
    ASSERT_TRUE(player->stop()); system.update(registry);
    EXPECT_DOUBLE_EQ(player->playback_time(), 0);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 0);
    EXPECT_FALSE(player->seek(-1)); EXPECT_FALSE(player->set_speed(101));
    registry->remove<commons::animation>(target.id);
    EXPECT_FALSE(player->play()); EXPECT_FALSE(player->pause()); EXPECT_FALSE(player->is_playing());
}

TEST_F(AnimationTest, RuntimeCrossFadeInterpolatesAndRejectsInvalidPathsWithoutMutation) {
    context->project_root = root;
    std::string error;
    ASSERT_TRUE(editor.create(target.id, "Source", error)) << error;
    auto source = clip(); source.duration = 2; source.loop = false;
    ASSERT_TRUE(editor.commit(target.id, source));
    auto destination = source;
    destination.tracks.front().keys = {{0,10},{2,10}};
    destination.tracks.push_back({commons::transform::mask, "position.y", false, {{0,10}}});
    registry->get<commons::transform>(target.id)->position.y = 4;
    std::ofstream(root / "assets/animations/Target.banim") << destination.encode();
    auto player = editor.player(target.id);
    double dt = .5;
    commons::animation_system system([&] { return dt; });
    system.update(registry);
    EXPECT_FALSE(player->cross_fade("../escape.banim", 1));
    EXPECT_FALSE(player->cross_fade("assets/animations/missing.banim", 1));
    EXPECT_FALSE(player->cross_fade("assets/animations/Target.banim", -1));
    ASSERT_TRUE(player->cross_fade("assets/animations/Target.banim", 1));
    system.update(registry); // source at .5 s, weight 0
    EXPECT_TRUE(player->is_blending());
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 1);
    ASSERT_TRUE(player->pause()); system.update(registry); system.update(registry);
    EXPECT_TRUE(player->is_blending()); EXPECT_FALSE(player->is_playing());
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 1);
    ASSERT_TRUE(player->play());
    system.update(registry); // source at 1 s, weight .5
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 6);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.y, 7);
    system.update(registry);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, 10);
    EXPECT_FALSE(player->is_blending());
    EXPECT_EQ(player->clip, "assets/animations/Source.banim");
    EXPECT_EQ(player->data->tracks, source.tracks);
    ASSERT_TRUE(player->cross_fade("assets/animations/Target.banim", 0));
    system.update(registry);
    EXPECT_FALSE(player->is_blending());
}

TEST_F(AnimationTest, CrossFadeHandlesDiscreteMissingAndReplacedTargetsAndInterruption) {
    context->project_root = root;
    std::string error;
    ASSERT_TRUE(editor.create(target.id, "Blend", error)) << error;
    registry->add<commons::sound_player>(target);
    auto source = clip();
    source.tracks.push_back({commons::sound_player::mask, "looping", true, {{0,0}}});
    ASSERT_TRUE(editor.commit(target.id, source));
    auto destination = source;
    destination.tracks.front().keys = {{0,10}};
    destination.tracks.back().keys = {{0,1}};
    std::ofstream(root / "assets/animations/Discrete.banim") << destination.encode();
    destination.tracks.front().keys = {{0,20}};
    std::ofstream(root / "assets/animations/Interrupted.banim") << destination.encode();
    auto player = editor.player(target.id);
    double dt = .5;
    commons::animation_system system([&] { return dt; });
    system.update(registry);
    ASSERT_TRUE(player->cross_fade("assets/animations/Discrete.banim", 1));
    system.update(registry);
    EXPECT_FALSE(registry->get<commons::sound_player>(target.id)->looping);
    system.update(registry);
    EXPECT_TRUE(registry->get<commons::sound_player>(target.id)->looping);
    const auto before = registry->get<commons::transform>(target.id)->position.x;
    ASSERT_TRUE(player->cross_fade("assets/animations/Interrupted.banim", 1));
    system.update(registry);
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(target.id)->position.x, before);
    registry->add<commons::sound_player>(target);
    system.update(registry);
    EXPECT_FALSE(registry->get<commons::sound_player>(target.id)->looping);
    registry->remove<commons::transform>(target.id);
    EXPECT_NO_THROW(system.update(registry));
    EXPECT_FALSE(player->is_blending());
    // Replaced components become normal live destinations after blending ends.
    system.update(registry);
    EXPECT_TRUE(registry->get<commons::sound_player>(target.id)->looping);
}
