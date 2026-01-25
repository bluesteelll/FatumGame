#include "GKSHull.h"

//still needs debugged. pretty sure it's laden with off-by-ones, sadly.
int FGKSHull::GrahamKurzerScan(std::vector<FVector2f>& cH, std::vector<FVector2f> Points)
{
	typedef std::pair<float, FVector2f> AngleKeyed;
	std::vector<FHRadixSort::ONUS> Angles(Points.size());
	
	std::vector<FVector2f> Angled(Points.size());
		
	auto a = std::max_element(Points.begin(), Points.end(), [](FVector2f a, FVector2f b)
	{
		return a.X < b.X;
	})->X;
	++a;
	auto b = std::min_element(Points.begin(), Points.end(), [](FVector2f a, FVector2f b)
	{
		return a.Y > b.Y;
	})->Y;
	--b;
	//we now have a point "outside" the set.
	//TODO: add handling for float max.
	auto synth = FVector2f(a, b);
	uint32 index = 0;
	for (auto p : Points)
	{
		float ang = synth.Y - p.Y / synth.X - p.X;
		Angles.push_back(FHRadixSort::ONUS(  *reinterpret_cast<uint32_t*>(&ang) , index));
	++index;
	}

	index = 0;
	for (auto keyed : Angles)
	{
		
		  Angled[index] = Points[keyed.b];
		++index;
	}
	//the point with the angle closest to vertical is going to end up in the hull
	
	int HullSize = 1;
	
	for (int i = 2; i <= Angles.size(); i++)
	{
		
		while (CCW(Points[i - 1], Points[HullSize], Points[i]) <= 0)
		{
			if (HullSize > 1) HullSize--;
			else if (i == Points.size()) break;
			else i++;
		}
		std::swap(Points[++HullSize], Points[i]);
	}

	for (int i = 1; i <= HullSize; i++) cH.push_back(Points[i]);
	return HullSize;
}
