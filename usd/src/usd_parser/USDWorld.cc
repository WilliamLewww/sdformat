/*
 * Copyright (C) 2022 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
#include "USDWorld.hh"

#include <memory>
#include <string>
#include <utility>

#pragma push_macro ("__DEPRECATED")
#undef __DEPRECATED
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdPhysics/scene.h>
#pragma pop_macro ("__DEPRECATED")

#include "sdf/usd/usd_parser/USDData.hh"
#include "sdf/usd/usd_parser/USDStage.hh"
#include "USDPhysics.hh"

#include "sdf/Plugin.hh"
#include "sdf/World.hh"

namespace sdf
{
inline namespace SDF_VERSION_NAMESPACE {
namespace usd
{
  UsdErrors parseUSDWorld(const std::string &_inputFileName,
    sdf::World &_world)
  {
    UsdErrors errors;
    USDData usdData(_inputFileName);
    errors = usdData.Init();
    if (!errors.empty())
      return errors;
    errors = usdData.ParseMaterials();
    if (!errors.empty())
      return errors;

    auto reference = pxr::UsdStage::Open(_inputFileName);
    if (!reference)
    {
      errors.emplace_back(UsdErrorCode::INVALID_USD_FILE,
        "Unable to open [" + _inputFileName + "]");
      return errors;
    }
    std::string worldName = reference->GetDefaultPrim().GetName().GetText();
    if (worldName.empty())
    {
      _world.SetName("world_name");
    }
    else
    {
      _world.SetName(worldName + "_world");
    }

    auto range = pxr::UsdPrimRange::Stage(reference);
    for (auto const &prim : range)
    {
      std::string primName = prim.GetName();

      if (prim.IsA<pxr::UsdPhysicsScene>())
      {
        std::pair<std::string, std::shared_ptr<USDStage>> data =
          usdData.FindStage(primName);
        if (!data.second)
        {
          errors.push_back(UsdError(UsdErrorCode::INVALID_PRIM_PATH,
                "Unable to retrieve the pxr::UsdPhysicsScene named ["
                + primName + "]"));
          return errors;
        }

        ParseUSDPhysicsScene(pxr::UsdPhysicsScene(prim), _world,
            data.second->MetersPerUnit());
        continue;
      }
    }

    // Add some plugins to run the Ignition Gazebo simulation
    sdf::Plugin physicsPlugin;
    physicsPlugin.SetName("ignition::gazebo::systems::Physics");
    physicsPlugin.SetFilename("ignition-gazebo-physics-system");
    _world.AddPlugin(physicsPlugin);

    sdf::Plugin sensorsPlugin;
    sensorsPlugin.SetName("ignition::gazebo::systems::Sensors");
    sensorsPlugin.SetFilename("ignition-gazebo-sensors-system");
    _world.AddPlugin(sensorsPlugin);

    sdf::Plugin userCommandsPlugin;
    userCommandsPlugin.SetName("ignition::gazebo::systems::UserCommands");
    userCommandsPlugin.SetFilename("ignition-gazebo-user-commands-system");
    _world.AddPlugin(userCommandsPlugin);

    sdf::Plugin sceneBroadcasterPlugin;
    sceneBroadcasterPlugin.SetName(
      "ignition::gazebo::systems::SceneBroadcaster");
    sceneBroadcasterPlugin.SetFilename(
      "ignition-gazebo-scene-broadcaster-system");
    _world.AddPlugin(sceneBroadcasterPlugin);

    return errors;
  }
}
}
}
