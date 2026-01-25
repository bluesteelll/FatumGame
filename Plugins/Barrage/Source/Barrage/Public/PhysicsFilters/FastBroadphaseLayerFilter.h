#pragma once

#include "EPhysicsLayer.h"
#include "IsolatedJoltIncludes.h"

class FastExcludeBroadphaseLayerFilter : public JPH::BroadPhaseLayerFilter
{
public:
	FastExcludeBroadphaseLayerFilter()
	{
		ExcludedLayers.reset();
	}
	
	FastExcludeBroadphaseLayerFilter(
		const std::vector<EPhysicsLayer>& LayersToExclude)
	{
		SetExcludedLayers(LayersToExclude);
	}

	virtual bool ShouldCollide(JPH::BroadPhaseLayer inLayer) const override
	{
		return !ExcludedLayers.test(inLayer.GetValue());
	}

	void SetExcludedLayers(
		const std::vector<EPhysicsLayer>& LayersToExclude)
	{
		ExcludedLayers.reset();
		for (const EPhysicsLayer& Layer : LayersToExclude)
		{
			ExcludedLayers.set(static_cast<size_t>(Layer));
		}
	}

private:
	std::bitset<Layers::NUM_LAYERS> ExcludedLayers; 
};

class FastIncludeBroadphaseLayerFilter : public JPH::BroadPhaseLayerFilter
{
public:
	FastIncludeBroadphaseLayerFilter()
	{
		IncludedLayers.reset();
	}

	FastIncludeBroadphaseLayerFilter(
		const std::vector<EPhysicsLayer>& LayersToExclude)
	{
		SetIncludedLayers(LayersToExclude);
	}

	FastIncludeBroadphaseLayerFilter(
		Layers::EJoltPhysicsLayer SingleLayer)
	{
		IncludedLayers.reset();
		IncludedLayers.set(static_cast<size_t>(SingleLayer));
	}

	virtual bool ShouldCollide(JPH::BroadPhaseLayer inLayer) const override
	{
		return IncludedLayers.test(inLayer.GetValue());
	}

	void SetIncludedLayers(
		const std::vector<EPhysicsLayer>& LayersToExclude)
	{
		IncludedLayers.reset();
		for (const EPhysicsLayer& Layer : LayersToExclude)
		{
			IncludedLayers.set(static_cast<size_t>(Layer));
		}
	}

private:
	std::bitset<Layers::NUM_LAYERS> IncludedLayers; 
};
