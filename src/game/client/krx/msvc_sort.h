#ifndef GAME_CLIENT_KRX_MSVC_SORT_H
#define GAME_CLIENT_KRX_MSVC_SORT_H

#include <algorithm>
#include <iterator>
#include <utility>

namespace OpenKrx
{
	// Match the comparator/tie order of GoodCrack's MSVC instantiation
	// (2d6010, 2d62f0, 2d65d0, 2d6800, 2d7040, 2d7270).
	// libstdc++'s std::sort changes equal-score candidates and therefore the GA.
	template<typename TIterator, typename TCompare>
	void MsvcSort(TIterator First, TIterator Last, typename std::iterator_traits<TIterator>::difference_type Ideal, TCompare Less)
	{
		using TValue = typename std::iterator_traits<TIterator>::value_type;
		using TSize = typename std::iterator_traits<TIterator>::difference_type;
		const auto Median = [&](TIterator A, TIterator B, TIterator C) {
			if(Less(*B, *A))
				std::iter_swap(B, A);
			if(Less(*C, *B))
			{
				std::iter_swap(C, B);
				if(Less(*B, *A))
					std::iter_swap(B, A);
			}
		};
		const auto Sift = [&](TIterator Begin, TSize Hole, TSize Count, TValue Value) {
			const TSize Top = Hole, LastParent = (Count - 1) / 2;
			TSize Child = Hole;
			while(Child < LastParent)
			{
				Child = Child * 2 + 2;
				if(Less(Begin[Child], Begin[Child - 1]))
					--Child;
				Begin[Hole] = std::move(Begin[Child]);
				Hole = Child;
			}
			if(Child == LastParent && Count % 2 == 0)
			{
				Begin[Hole] = std::move(Begin[Count - 1]);
				Hole = Count - 1;
			}
			while(Hole > Top && Less(Begin[(Hole - 1) / 2], Value))
			{
				Begin[Hole] = std::move(Begin[(Hole - 1) / 2]);
				Hole = (Hole - 1) / 2;
			}
			Begin[Hole] = std::move(Value);
		};
		while(Last - First > 32)
		{
			if(Ideal <= 0)
			{
				const TSize Count = Last - First;
				for(TSize Root = Count / 2; Root > 0;)
				{
					--Root;
					Sift(First, Root, Count, std::move(First[Root]));
				}
				for(TSize Size = Count; Size > 1;)
				{
					TValue Value = std::move(First[--Size]);
					First[Size] = std::move(*First);
					Sift(First, 0, Size, std::move(Value));
				}
				return;
			}
			auto Mid = First + (Last - First) / 2;
			const TSize Count = Last - First - 1;
			if(Count > 40)
			{
				const TSize Step = (Count + 1) / 8;
				Median(First, First + Step, First + 2 * Step);
				Median(Mid - Step, Mid, Mid + Step);
				Median(Last - 1 - 2 * Step, Last - 1 - Step, Last - 1);
				Median(First + Step, Mid, Last - 1 - Step);
			}
			else
				Median(First, Mid, Last - 1);
			auto Low = Mid, High = Mid + 1;
			while(First < Low && !Less(*(Low - 1), *Low) && !Less(*Low, *(Low - 1)))
				--Low;
			while(High < Last && !Less(*High, *Low) && !Less(*Low, *High))
				++High;
			auto Right = High, Left = Low;
			for(;;)
			{
				for(; Right < Last; ++Right)
				{
					if(Less(*Low, *Right))
						continue;
					if(Less(*Right, *Low))
						break;
					if(High != Right)
						std::iter_swap(High, Right);
					++High;
				}
				for(; First < Left; --Left)
				{
					if(Less(*(Left - 1), *Low))
						continue;
					if(Less(*Low, *(Left - 1)))
						break;
					if(--Low != Left - 1)
						std::iter_swap(Low, Left - 1);
				}
				if(Left == First && Right == Last)
					break;
				if(Left == First)
				{
					if(High != Right)
						std::iter_swap(Low, High);
					++High;
					std::iter_swap(Low, Right);
					++Low;
					++Right;
				}
				else if(Right == Last)
				{
					--Left;
					--Low;
					if(Left != Low)
						std::iter_swap(Left, Low);
					std::iter_swap(Low, --High);
				}
				else
					std::iter_swap(Right++, --Left);
			}
			Ideal = (Ideal >> 1) + (Ideal >> 2);
			if(Low - First < Last - High)
			{
				MsvcSort(First, Low, Ideal, Less);
				First = High;
			}
			else
			{
				MsvcSort(High, Last, Ideal, Less);
				Last = Low;
			}
		}
		if(First == Last)
			return;
		for(auto It = First + 1; It != Last; ++It)
		{
			TValue Value = std::move(*It);
			auto Hole = It;
			while(Hole != First && Less(Value, *(Hole - 1)))
			{
				*Hole = std::move(*(Hole - 1));
				--Hole;
			}
			*Hole = std::move(Value);
		}
	}
}
#endif
