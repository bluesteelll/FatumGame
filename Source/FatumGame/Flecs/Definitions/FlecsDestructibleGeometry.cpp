#include "FlecsDestructibleGeometry.h"
#include "Engine/StaticMesh.h"

#if WITH_EDITOR
void UFlecsDestructibleGeometry::GenerateAdjacencyFromProximity()
{
	AdjacencyLinks.Empty();

	for (int32 i = 0; i < Fragments.Num(); ++i)
	{
		for (int32 j = i + 1; j < Fragments.Num(); ++j)
		{
			const float Dist = FVector::Dist(
				Fragments[i].RelativeTransform.GetLocation(),
				Fragments[j].RelativeTransform.GetLocation());

			float MaxGap = AdjacencyThreshold;

			if (Fragments[i].Mesh && Fragments[j].Mesh)
			{
				const FVector ExtentsA = Fragments[i].Mesh->GetBoundingBox().GetExtent()
					* Fragments[i].RelativeTransform.GetScale3D();
				const FVector ExtentsB = Fragments[j].Mesh->GetBoundingBox().GetExtent()
					* Fragments[j].RelativeTransform.GetScale3D();
				MaxGap += (ExtentsA + ExtentsB).GetMax();
			}

			if (Dist <= MaxGap)
			{
				FFragmentAdjacency Link;
				Link.FragmentIndexA = i;
				Link.FragmentIndexB = j;
				AdjacencyLinks.Add(Link);
			}
		}
	}

	Modify();
	UE_LOG(LogTemp, Log, TEXT("UFlecsDestructibleGeometry: Generated %d adjacency links for %d fragments"),
		AdjacencyLinks.Num(), Fragments.Num());
}
#endif
