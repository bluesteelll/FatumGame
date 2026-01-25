#pragma once

struct ParticleRecord
{
	TWeakObjectPtr<UNiagaraDataChannelAsset> NDCAssetPtr;
	int32 NDCIndex;
};
