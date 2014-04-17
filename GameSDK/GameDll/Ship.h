#ifndef _Ship_
#define _Ship_

#include <IGameObject.h>


struct SShipParams {};

class CShip : public CGameObjectExtensionHelper<CShip, IGameObjectExtension>
{
public:
	CShip();
	virtual ~CShip();

	///////////////////
	virtual bool Init(IGameObject *pGameObject);
	virtual void InitClient(int channelId) {};
	virtual void PostInit(IGameObject *pGameObject);
	virtual void PostInitClient(int channelId) {};
	virtual bool ReloadExtension( IGameObject * pGameObject, const SEntitySpawnParams &params );
	virtual void PostReloadExtension( IGameObject * pGameObject, const SEntitySpawnParams &params ) {}
	virtual bool GetEntityPoolSignature( TSerialize signature );
	virtual void Release();
	virtual bool NetSerialize(TSerialize ser, EEntityAspects aspect, uint8 profile, int pflags) { return true; }
	virtual void FullSerialize(TSerialize ser);
	virtual void PostSerialize() {}
	virtual void SerializeSpawnInfo( TSerialize ser ) {}
	virtual ISerializableInfoPtr GetSpawnInfo() {return 0;}
	virtual void Update( SEntityUpdateContext &ctx, int updateSlot);
	virtual void PostUpdate(float frameTime ) {};
	virtual void PostRemoteSpawn() {};
	virtual void HandleEvent( const SGameObjectEvent &);
	virtual void ProcessEvent(SEntityEvent &);
	virtual void SetChannelId(uint16 id) {}
	virtual void SetAuthority(bool auth);
	virtual void GetMemoryUsage(ICrySizer *pSizer) const { pSizer->Add(*this); }
	virtual void OnHit(const HitInfo* hitInfo);

	//~IGameObjectExtension

	bool Reset();

	protected:

	void PreloadTextures();

	SShipParams	m_params;
	bool				m_bEnabled;

	typedef std::vector<ITexture*> TTextureList;
	TTextureList m_Textures;

private:
	CShip(const CShip&);
	//CRain& operator = (const CRain&);
};

#endif