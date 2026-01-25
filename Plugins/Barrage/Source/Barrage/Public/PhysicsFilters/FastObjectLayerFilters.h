#pragma once

#include "EPhysicsLayer.h"
#include "IsolatedJoltIncludes.h"

class FastExcludeObjectLayerFilter : public JPH::ObjectLayerFilter
{
public:
	FastExcludeObjectLayerFilter()
	{
		ExcludedLayers.reset();
	}
	
	FastExcludeObjectLayerFilter(
		const std::vector<EPhysicsLayer>& LayersToExclude)
	{
		SetExcludedLayers(LayersToExclude);
	}

	virtual bool ShouldCollide(JPH::ObjectLayer inLayer) const override
	{
		return !ExcludedLayers.test(inLayer);
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

class FastIncludeObjectLayerFilter : public JPH::ObjectLayerFilter
{
public:
	FastIncludeObjectLayerFilter()
	{
		IncludedLayers.reset();
	}
	
	FastIncludeObjectLayerFilter(
		const std::vector<EPhysicsLayer>& LayersToExclude)
	{
		SetIncludedLayers(LayersToExclude);
	}

	FastIncludeObjectLayerFilter(
		Layers::EJoltPhysicsLayer SingleLayer)
	{
		IncludedLayers.reset();
		IncludedLayers.set(static_cast<size_t>(SingleLayer));
	}

	virtual bool ShouldCollide(JPH::ObjectLayer inLayer) const override
	{
		return IncludedLayers.test(inLayer);
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
