#pragma once
namespace SOUI
{
	class SOUI_EXP SDpiScale
	{
	public:
		static int GetBuiltinScaleCount();

		static const int * GetBuiltinScales();

		/**
        * NormalizeScale
        * @brief    ��׼���Ŵ����, ѡ����Լ�ָ������Сһ�ŵı���
        * @param [in] nScale -- input scale
        * @return   int -- normalized scale
        *
        * Describe  
        */
		static int NormalizeScale(int nScale);
	};


}
