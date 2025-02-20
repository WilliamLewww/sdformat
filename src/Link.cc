/*
 * Copyright 2018 Open Source Robotics Foundation
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
#include <memory>
#include <string>
#include <vector>
#include <ignition/math/Inertial.hh>
#include <ignition/math/Pose3.hh>
#include <ignition/math/Vector3.hh>

#include "sdf/Collision.hh"
#include "sdf/Error.hh"
#include "sdf/Light.hh"
#include "sdf/Link.hh"
#include "sdf/parser.hh"
#include "sdf/ParticleEmitter.hh"
#include "sdf/Sensor.hh"
#include "sdf/Types.hh"
#include "sdf/Visual.hh"

#include "FrameSemantics.hh"
#include "ScopedGraph.hh"
#include "Utils.hh"

using namespace sdf;

class sdf::Link::Implementation
{
  /// \brief Name of the link.
  public: std::string name = "";

  /// \brief Pose of the link
  public: ignition::math::Pose3d pose = ignition::math::Pose3d::Zero;

  /// \brief Frame of the pose.
  public: std::string poseRelativeTo = "";

  /// \brief The visuals specified in this link.
  public: std::vector<Visual> visuals;

  /// \brief The lights specified in this link.
  public: std::vector<Light> lights;

  /// \brief The collisions specified in this link.
  public: std::vector<Collision> collisions;

  /// \brief The sensors specified in this link.
  public: std::vector<Sensor> sensors;

  /// \brief The particle emitters specified in this link.
  public: std::vector<ParticleEmitter> emitters;

  /// \brief The inertial information for this link.
  public: ignition::math::Inertiald inertial {{1.0,
            ignition::math::Vector3d::One, ignition::math::Vector3d::Zero},
            ignition::math::Pose3d::Zero};

  /// \brief The SDF element pointer used during load.
  public: sdf::ElementPtr sdf;

  /// \brief True if this link should be subject to wind, false otherwise.
  public: bool enableWind = false;

  /// \brief Scoped Pose Relative-To graph at the parent model scope.
  public: sdf::ScopedGraph<sdf::PoseRelativeToGraph> poseRelativeToGraph;
};

/////////////////////////////////////////////////
Link::Link()
  : dataPtr(ignition::utils::MakeImpl<Implementation>())
{
}

/////////////////////////////////////////////////
Errors Link::Load(ElementPtr _sdf)
{
  Errors errors;

  this->dataPtr->sdf = _sdf;

  // Check that the provided SDF element is a <link>
  // This is an error that cannot be recovered, so return an error.
  if (_sdf->GetName() != "link")
  {
    errors.push_back({ErrorCode::ELEMENT_INCORRECT_TYPE,
        "Attempting to load a Link, but the provided SDF element is not a "
        "<link>."});
    return errors;
  }

  // Read the links's name
  if (!loadName(_sdf, this->dataPtr->name))
  {
    errors.push_back({ErrorCode::ATTRIBUTE_MISSING,
                     "A link name is required, but the name is not set."});
  }

  // Check that the link's name is valid
  if (isReservedName(this->dataPtr->name))
  {
    errors.push_back({ErrorCode::RESERVED_NAME,
                     "The supplied link name [" + this->dataPtr->name +
                     "] is reserved."});
  }

  // Load the pose. Ignore the return value since the pose is optional.
  loadPose(_sdf, this->dataPtr->pose, this->dataPtr->poseRelativeTo);

  // Load all the visuals.
  Errors visLoadErrors = loadUniqueRepeated<Visual>(_sdf, "visual",
      this->dataPtr->visuals);
  errors.insert(errors.end(), visLoadErrors.begin(), visLoadErrors.end());

  // Load all the collisions.
  Errors collLoadErrors = loadUniqueRepeated<Collision>(_sdf, "collision",
      this->dataPtr->collisions);
  errors.insert(errors.end(), collLoadErrors.begin(), collLoadErrors.end());

  // Load all the lights.
  Errors lightLoadErrors = loadUniqueRepeated<Light>(_sdf, "light",
      this->dataPtr->lights);
  errors.insert(errors.end(), lightLoadErrors.begin(), lightLoadErrors.end());

  // Load all the sensors.
  Errors sensorLoadErrors = loadUniqueRepeated<Sensor>(_sdf, "sensor",
      this->dataPtr->sensors);
  errors.insert(errors.end(), sensorLoadErrors.begin(), sensorLoadErrors.end());

  // Load all the particle emitters.
  Errors emitterLoadErrors = loadUniqueRepeated<ParticleEmitter>(_sdf,
      "particle_emitter", this->dataPtr->emitters);
  errors.insert(errors.end(), emitterLoadErrors.begin(),
      emitterLoadErrors.end());

  ignition::math::Vector3d xxyyzz = ignition::math::Vector3d::One;
  ignition::math::Vector3d xyxzyz = ignition::math::Vector3d::Zero;
  ignition::math::Pose3d inertiaPose;
  std::string inertiaFrame = "";
  double mass = 1.0;

  if (_sdf->HasElement("inertial"))
  {
    sdf::ElementPtr inertialElem = _sdf->GetElement("inertial");

    if (inertialElem->HasElement("pose"))
      loadPose(inertialElem->GetElement("pose"), inertiaPose, inertiaFrame);

    // Get the mass.
    mass = inertialElem->Get<double>("mass", 1.0).first;

    if (inertialElem->HasElement("inertia"))
    {
      sdf::ElementPtr inertiaElem = inertialElem->GetElement("inertia");

      xxyyzz.X(inertiaElem->Get<double>("ixx", 1.0).first);
      xxyyzz.Y(inertiaElem->Get<double>("iyy", 1.0).first);
      xxyyzz.Z(inertiaElem->Get<double>("izz", 1.0).first);

      xyxzyz.X(inertiaElem->Get<double>("ixy", 0.0).first);
      xyxzyz.Y(inertiaElem->Get<double>("ixz", 0.0).first);
      xyxzyz.Z(inertiaElem->Get<double>("iyz", 0.0).first);
    }
  }
  if (!this->dataPtr->inertial.SetMassMatrix(
      ignition::math::MassMatrix3d(mass, xxyyzz, xyxzyz)))
  {
    errors.push_back({ErrorCode::LINK_INERTIA_INVALID,
                     "A link named " +
                     this->Name() +
                     " has invalid inertia."});
  }

  /// \todo: Handle inertia frame properly
  this->dataPtr->inertial.SetPose(inertiaPose);

  this->dataPtr->enableWind = _sdf->Get<bool>("enable_wind",
      this->dataPtr->enableWind).first;

  return errors;
}

/////////////////////////////////////////////////
std::string Link::Name() const
{
  return this->dataPtr->name;
}

/////////////////////////////////////////////////
void Link::SetName(const std::string &_name)
{
  this->dataPtr->name = _name;
}

/////////////////////////////////////////////////
uint64_t Link::VisualCount() const
{
  return this->dataPtr->visuals.size();
}

/////////////////////////////////////////////////
const Visual *Link::VisualByIndex(const uint64_t _index) const
{
  if (_index < this->dataPtr->visuals.size())
    return &this->dataPtr->visuals[_index];
  return nullptr;
}

/////////////////////////////////////////////////
Visual *Link::VisualByIndex(uint64_t _index)
{
  return const_cast<Visual*>(
      static_cast<const Link*>(this)->VisualByIndex(_index));
}

/////////////////////////////////////////////////
bool Link::VisualNameExists(const std::string &_name) const
{
  for (auto const &v : this->dataPtr->visuals)
  {
    if (v.Name() == _name)
    {
      return true;
    }
  }
  return false;
}

/////////////////////////////////////////////////
uint64_t Link::CollisionCount() const
{
  return this->dataPtr->collisions.size();
}

/////////////////////////////////////////////////
const Collision *Link::CollisionByIndex(const uint64_t _index) const
{
  if (_index < this->dataPtr->collisions.size())
    return &this->dataPtr->collisions[_index];
  return nullptr;
}

/////////////////////////////////////////////////
Collision *Link::CollisionByIndex(uint64_t _index)
{
  return const_cast<Collision*>(
      static_cast<const Link*>(this)->CollisionByIndex(_index));
}

/////////////////////////////////////////////////
bool Link::CollisionNameExists(const std::string &_name) const
{
  for (auto const &c : this->dataPtr->collisions)
  {
    if (c.Name() == _name)
    {
      return true;
    }
  }
  return false;
}

/////////////////////////////////////////////////
uint64_t Link::LightCount() const
{
  return this->dataPtr->lights.size();
}

/////////////////////////////////////////////////
const Light *Link::LightByIndex(const uint64_t _index) const
{
  if (_index < this->dataPtr->lights.size())
    return &this->dataPtr->lights[_index];
  return nullptr;
}

/////////////////////////////////////////////////
Light *Link::LightByIndex(uint64_t _index)
{
  return const_cast<Light*>(
      static_cast<const Link*>(this)->LightByIndex(_index));
}

/////////////////////////////////////////////////
bool Link::LightNameExists(const std::string &_name) const
{
  return this->LightByName(_name) != nullptr;
}

/////////////////////////////////////////////////
uint64_t Link::SensorCount() const
{
  return this->dataPtr->sensors.size();
}

/////////////////////////////////////////////////
const Sensor *Link::SensorByIndex(const uint64_t _index) const
{
  if (_index < this->dataPtr->sensors.size())
    return &this->dataPtr->sensors[_index];
  return nullptr;
}

/////////////////////////////////////////////////
Sensor *Link::SensorByIndex(uint64_t _index)
{
  return const_cast<Sensor*>(
      static_cast<const Link*>(this)->SensorByIndex(_index));
}

/////////////////////////////////////////////////
bool Link::SensorNameExists(const std::string &_name) const
{
  for (auto const &s : this->dataPtr->sensors)
  {
    if (s.Name() == _name)
    {
      return true;
    }
  }
  return false;
}

/////////////////////////////////////////////////
const Sensor *Link::SensorByName(const std::string &_name) const
{
  for (auto const &s : this->dataPtr->sensors)
  {
    if (s.Name() == _name)
    {
      return &s;
    }
  }
  return nullptr;
}

/////////////////////////////////////////////////
Sensor *Link::SensorByName(const std::string &_name)
{
  return const_cast<Sensor*>(
      static_cast<const Link*>(this)->SensorByName(_name));
}

/////////////////////////////////////////////////
uint64_t Link::ParticleEmitterCount() const
{
  return this->dataPtr->emitters.size();
}

/////////////////////////////////////////////////
const ParticleEmitter *Link::ParticleEmitterByIndex(const uint64_t _index) const
{
  if (_index < this->dataPtr->emitters.size())
    return &this->dataPtr->emitters[_index];
  return nullptr;
}

/////////////////////////////////////////////////
ParticleEmitter *Link::ParticleEmitterByIndex(uint64_t _index)
{
  return const_cast<ParticleEmitter*>(
      static_cast<const Link*>(this)->ParticleEmitterByIndex(_index));
}

/////////////////////////////////////////////////
bool Link::ParticleEmitterNameExists(const std::string &_name) const
{
  for (auto const &e : this->dataPtr->emitters)
  {
    if (e.Name() == _name)
    {
      return true;
    }
  }
  return false;
}

/////////////////////////////////////////////////
const ParticleEmitter *Link::ParticleEmitterByName(
    const std::string &_name) const
{
  for (auto const &e : this->dataPtr->emitters)
  {
    if (e.Name() == _name)
    {
      return &e;
    }
  }
  return nullptr;
}

/////////////////////////////////////////////////
ParticleEmitter *Link::ParticleEmitterByName(const std::string &_name)
{
  return const_cast<ParticleEmitter *>(
      static_cast<const Link*>(this)->ParticleEmitterByName(_name));
}

/////////////////////////////////////////////////
const ignition::math::Inertiald &Link::Inertial() const
{
  return this->dataPtr->inertial;
}

/////////////////////////////////////////////////
bool Link::SetInertial(const ignition::math::Inertiald &_inertial)
{
  this->dataPtr->inertial = _inertial;
  return _inertial.MassMatrix().IsValid();
}

/////////////////////////////////////////////////
const ignition::math::Pose3d &Link::RawPose() const
{
  return this->dataPtr->pose;
}

/////////////////////////////////////////////////
const std::string &Link::PoseRelativeTo() const
{
  return this->dataPtr->poseRelativeTo;
}

/////////////////////////////////////////////////
void Link::SetRawPose(const ignition::math::Pose3d &_pose)
{
  this->dataPtr->pose = _pose;
}

/////////////////////////////////////////////////
void Link::SetPoseRelativeTo(const std::string &_frame)
{
  this->dataPtr->poseRelativeTo = _frame;
}

/////////////////////////////////////////////////
void Link::SetPoseRelativeToGraph(sdf::ScopedGraph<PoseRelativeToGraph> _graph)
{
  this->dataPtr->poseRelativeToGraph = _graph;

  // Pass graph to child elements.
  for (auto &collision : this->dataPtr->collisions)
  {
    collision.SetXmlParentName(this->dataPtr->name);
    collision.SetPoseRelativeToGraph(_graph);
  }
  for (auto &light : this->dataPtr->lights)
  {
    light.SetXmlParentName(this->dataPtr->name);
    light.SetPoseRelativeToGraph(_graph);
  }
  for (auto &sensor : this->dataPtr->sensors)
  {
    sensor.SetXmlParentName(this->dataPtr->name);
    sensor.SetPoseRelativeToGraph(_graph);
  }
  for (auto &visual : this->dataPtr->visuals)
  {
    visual.SetXmlParentName(this->dataPtr->name);
    visual.SetPoseRelativeToGraph(_graph);
  }

  for (auto &emitter : this->dataPtr->emitters)
  {
    emitter.SetXmlParentName(this->dataPtr->name);
    emitter.SetPoseRelativeToGraph(_graph);
  }
}

/////////////////////////////////////////////////
sdf::SemanticPose Link::SemanticPose() const
{
  return sdf::SemanticPose(
      this->dataPtr->name,
      this->dataPtr->pose,
      this->dataPtr->poseRelativeTo,
      "__model__",
      this->dataPtr->poseRelativeToGraph);
}

/////////////////////////////////////////////////
const Visual *Link::VisualByName(const std::string &_name) const
{
  for (auto const &v : this->dataPtr->visuals)
  {
    if (v.Name() == _name)
    {
      return &v;
    }
  }
  return nullptr;
}

/////////////////////////////////////////////////
Visual *Link::VisualByName(const std::string &_name)
{
  return const_cast<Visual *>(
      static_cast<const Link*>(this)->VisualByName(_name));
}

/////////////////////////////////////////////////
const Collision *Link::CollisionByName(const std::string &_name) const
{
  for (auto &c : this->dataPtr->collisions)
  {
    if (c.Name() == _name)
    {
      return &c;
    }
  }
  return nullptr;
}

/////////////////////////////////////////////////
Collision *Link::CollisionByName(const std::string &_name)
{
  return const_cast<Collision *>(
      static_cast<const Link*>(this)->CollisionByName(_name));
}

/////////////////////////////////////////////////
const Light *Link::LightByName(const std::string &_name) const
{
  for (auto const &c : this->dataPtr->lights)
  {
    if (c.Name() == _name)
    {
      return &c;
    }
  }
  return nullptr;
}

/////////////////////////////////////////////////
Light *Link::LightByName(const std::string &_name)
{
  return const_cast<Light *>(
      static_cast<const Link*>(this)->LightByName(_name));

}

/////////////////////////////////////////////////
sdf::ElementPtr Link::Element() const
{
  return this->dataPtr->sdf;
}

/////////////////////////////////////////////////
bool Link::EnableWind() const
{
  return this->dataPtr->enableWind;
}

/////////////////////////////////////////////////
void Link::SetEnableWind(const bool _enableWind)
{
  this->dataPtr->enableWind = _enableWind;
}

//////////////////////////////////////////////////
bool Link::AddCollision(const Collision &_collision)
{
  if (this->CollisionNameExists(_collision.Name()))
    return false;
  this->dataPtr->collisions.push_back(_collision);
  return true;
}

//////////////////////////////////////////////////
bool Link::AddVisual(const Visual &_visual)
{
  if (this->VisualNameExists(_visual.Name()))
    return false;
  this->dataPtr->visuals.push_back(_visual);
  return true;
}

//////////////////////////////////////////////////
bool Link::AddLight(const Light &_light)
{
  if (this->LightNameExists(_light.Name()))
    return false;
  this->dataPtr->lights.push_back(_light);
  return true;
}

//////////////////////////////////////////////////
bool Link::AddSensor(const Sensor &_sensor)
{
  if (this->SensorNameExists(_sensor.Name()))
    return false;
  this->dataPtr->sensors.push_back(_sensor);
  return true;
}

//////////////////////////////////////////////////
bool Link::AddParticleEmitter(const ParticleEmitter &_emitter)
{
  if (this->ParticleEmitterNameExists(_emitter.Name()))
    return false;
  this->dataPtr->emitters.push_back(_emitter);
  return true;
}

//////////////////////////////////////////////////
void Link::ClearCollisions()
{
  this->dataPtr->collisions.clear();
}

//////////////////////////////////////////////////
void Link::ClearVisuals()
{
  this->dataPtr->visuals.clear();
}

//////////////////////////////////////////////////
void Link::ClearLights()
{
  this->dataPtr->lights.clear();
}

//////////////////////////////////////////////////
void Link::ClearSensors()
{
  this->dataPtr->sensors.clear();
}

//////////////////////////////////////////////////
void Link::ClearParticleEmitters()
{
  this->dataPtr->emitters.clear();
}

/////////////////////////////////////////////////
sdf::ElementPtr Link::ToElement() const
{
  sdf::ElementPtr elem(new sdf::Element);
  sdf::initFile("link.sdf", elem);

  elem->GetAttribute("name")->Set(this->Name());

  // Set pose
  sdf::ElementPtr poseElem = elem->GetElement("pose");
  if (!this->dataPtr->poseRelativeTo.empty())
  {
    poseElem->GetAttribute("relative_to")->Set<std::string>(
        this->dataPtr->poseRelativeTo);
  }
  poseElem->Set<ignition::math::Pose3d>(this->RawPose());

  // inertial
  sdf::ElementPtr inertialElem = elem->GetElement("inertial");
  inertialElem->GetElement("pose")->Set(
      this->dataPtr->inertial.Pose());
  const ignition::math::MassMatrix3d &massMatrix =
    this->dataPtr->inertial.MassMatrix();
  inertialElem->GetElement("mass")->Set<double>(massMatrix.Mass());
  sdf::ElementPtr inertiaElem = inertialElem->GetElement("inertia");
  inertiaElem->GetElement("ixx")->Set(massMatrix.Ixx());
  inertiaElem->GetElement("ixy")->Set(massMatrix.Ixy());
  inertiaElem->GetElement("ixz")->Set(massMatrix.Ixz());
  inertiaElem->GetElement("iyy")->Set(massMatrix.Iyy());
  inertiaElem->GetElement("iyz")->Set(massMatrix.Iyz());
  inertiaElem->GetElement("izz")->Set(massMatrix.Izz());

  // wind mode
  elem->GetElement("enable_wind")->Set(this->EnableWind());

  // Collisions
  for (const sdf::Collision &collision : this->dataPtr->collisions)
  {
    elem->InsertElement(collision.ToElement(), true);
  }

  // Light
  for (const sdf::Light &light : this->dataPtr->lights)
  {
    elem->InsertElement(light.ToElement(), true);
  }

  // Particle emitters
  for (const sdf::ParticleEmitter &emitter : this->dataPtr->emitters)
  {
    elem->InsertElement(emitter.ToElement(), true);
  }

  // Sensors
  for (const sdf::Sensor &sensor : this->dataPtr->sensors)
  {
    elem->InsertElement(sensor.ToElement(), true);
  }

  // Visuals
  for (const sdf::Visual &visual : this->dataPtr->visuals)
  {
    elem->InsertElement(visual.ToElement(), true);
  }

  return elem;
}
