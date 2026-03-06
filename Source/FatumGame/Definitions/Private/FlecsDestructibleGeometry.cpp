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

void UFlecsDestructibleGeometry::GenerateColliderBounds()
{
	int32 Updated = 0;
	for (FDestructibleFragment& Frag : Fragments)
	{
		if (!Frag.Mesh) continue;

		const FVector MeshExtent = Frag.Mesh->GetBoundingBox().GetExtent();
		FVector Scaled = MeshExtent * Frag.RelativeTransform.GetScale3D() * ColliderScale;
		Scaled.X = FMath::Max(Scaled.X, 1.0f);
		Scaled.Y = FMath::Max(Scaled.Y, 1.0f);
		Scaled.Z = FMath::Max(Scaled.Z, 1.0f);
		Frag.ColliderHalfExtents = Scaled;
		++Updated;
	}

	Modify();
	UE_LOG(LogTemp, Log, TEXT("UFlecsDestructibleGeometry: Generated collider bounds for %d/%d fragments (Scale=%.2f)"),
		Updated, Fragments.Num(), ColliderScale);
}
#endif
