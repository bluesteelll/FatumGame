// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "FlecsConstrainedGroupDefinition.h"
#include "Engine/StaticMesh.h"

void UFlecsConstrainedGroupDefinition::GenerateChain(UStaticMesh* Mesh, int32 Count, FVector Spacing, EFlecsConstraintType LinkType, float BreakForce)
{
	if (!Mesh || Count < 2) return;

	Elements.Empty();
	Constraints.Empty();

	// Create elements
	for (int32 i = 0; i < Count; ++i)
	{
		FFlecsGroupElement Element;
		Element.ElementName = FName(*FString::Printf(TEXT("Link_%d"), i));
		Element.Mesh = Mesh;
		Element.LocalOffset = Spacing * static_cast<float>(i);
		Element.bIsMovable = true;
		Elements.Add(Element);
	}

	// Create constraints between adjacent elements
	for (int32 i = 0; i < Count - 1; ++i)
	{
		FFlecsGroupConstraint Constraint;
		Constraint.Element1Index = i;
		Constraint.Element2Index = i + 1;
		Constraint.ConstraintType = LinkType;
		Constraint.BreakForce = BreakForce;

		// Set anchor points at the connection between elements
		Constraint.AnchorOffset1 = Spacing * 0.5f;
		Constraint.AnchorOffset2 = -Spacing * 0.5f;

		Constraints.Add(Constraint);
	}
}

void UFlecsConstrainedGroupDefinition::GenerateGrid(UStaticMesh* Mesh, int32 Rows, int32 Columns, FVector Spacing, EFlecsConstraintType LinkType, float BreakForce)
{
	if (!Mesh || Rows < 1 || Columns < 1) return;

	Elements.Empty();
	Constraints.Empty();

	// Create elements in grid
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < Columns; ++Col)
		{
			FFlecsGroupElement Element;
			Element.ElementName = FName(*FString::Printf(TEXT("Grid_%d_%d"), Row, Col));
			Element.Mesh = Mesh;
			Element.LocalOffset = FVector(Spacing.X * Col, Spacing.Y * Row, 0.f);
			Element.bIsMovable = true;
			Elements.Add(Element);
		}
	}

	// Create horizontal constraints
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < Columns - 1; ++Col)
		{
			int32 Index1 = Row * Columns + Col;
			int32 Index2 = Row * Columns + Col + 1;

			FFlecsGroupConstraint Constraint;
			Constraint.Element1Index = Index1;
			Constraint.Element2Index = Index2;
			Constraint.ConstraintType = LinkType;
			Constraint.BreakForce = BreakForce;
			Constraint.AnchorOffset1 = FVector(Spacing.X * 0.5f, 0.f, 0.f);
			Constraint.AnchorOffset2 = FVector(-Spacing.X * 0.5f, 0.f, 0.f);
			Constraints.Add(Constraint);
		}
	}

	// Create vertical constraints
	for (int32 Row = 0; Row < Rows - 1; ++Row)
	{
		for (int32 Col = 0; Col < Columns; ++Col)
		{
			int32 Index1 = Row * Columns + Col;
			int32 Index2 = (Row + 1) * Columns + Col;

			FFlecsGroupConstraint Constraint;
			Constraint.Element1Index = Index1;
			Constraint.Element2Index = Index2;
			Constraint.ConstraintType = LinkType;
			Constraint.BreakForce = BreakForce;
			Constraint.AnchorOffset1 = FVector(0.f, Spacing.Y * 0.5f, 0.f);
			Constraint.AnchorOffset2 = FVector(0.f, -Spacing.Y * 0.5f, 0.f);
			Constraints.Add(Constraint);
		}
	}
}

bool UFlecsConstrainedGroupDefinition::IsValid() const
{
	if (Elements.Num() == 0) return false;

	for (const FFlecsGroupConstraint& Constraint : Constraints)
	{
		if (Constraint.Element1Index < 0 || Constraint.Element1Index >= Elements.Num())
			return false;
		if (Constraint.Element2Index < 0 || Constraint.Element2Index >= Elements.Num())
			return false;
		if (Constraint.Element1Index == Constraint.Element2Index)
			return false;
	}

	// Check all elements have meshes
	for (const FFlecsGroupElement& Element : Elements)
	{
		if (!Element.Mesh)
			return false;
	}

	return true;
}

#if WITH_EDITOR
void UFlecsConstrainedGroupDefinition::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Validate constraint indices
	for (FFlecsGroupConstraint& Constraint : Constraints)
	{
		Constraint.Element1Index = FMath::Clamp(Constraint.Element1Index, 0, FMath::Max(0, Elements.Num() - 1));
		Constraint.Element2Index = FMath::Clamp(Constraint.Element2Index, 0, FMath::Max(0, Elements.Num() - 1));
	}
}
#endif
