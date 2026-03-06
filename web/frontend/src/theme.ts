import type { GlobalThemeOverrides } from 'naive-ui'

export const lightThemeOverrides: GlobalThemeOverrides = {
  common: {
    primaryColor: '#0B7FCF',
    primaryColorHover: '#0A73BC',
    primaryColorPressed: '#0868AA',

    infoColor: '#2C8FD6',
    successColor: '#2C9A63',
    warningColor: '#C58A18',
    errorColor: '#C74747',

    bodyColor: 'transparent',
    cardColor: 'rgba(255, 255, 255, 0.7)',
    popoverColor: 'rgba(255, 255, 255, 0.85)',
    modalColor: 'rgba(255, 255, 255, 0.85)',

    textColorBase: '#1B2430',
    textColor1: '#1B2430',
    textColor2: '#4A5667',
    textColor3: '#6F7C8E',
    textColorDisabled: '#98A3B3',

    borderColor: 'rgba(216, 224, 234, 0.6)',
    dividerColor: 'rgba(233, 238, 245, 0.6)',

    borderRadius: '12px',
    borderRadiusSmall: '8px',
  },
  Button: {
    heightMedium: '40px',
    heightSmall: '32px',
    heightLarge: '48px',
    borderRadiusMedium: '10px',
    borderRadiusSmall: '8px',
    borderRadiusLarge: '12px',
    fontWeight: '600',
  },
  Input: {
    heightMedium: '40px',
    heightSmall: '32px',
    heightLarge: '48px',
    borderRadius: '10px',
    color: '#FFFFFF',
    placeholderColor: 'rgba(111, 124, 142, 0.72)',
  },
  Select: {
    peers: {
      InternalSelection: {
        heightMedium: '40px',
        borderRadius: '10px',
      },
    },
  },
  Card: {
    borderRadius: '14px',
    color: 'rgba(255, 255, 255, 0.7)',
  },
  Alert: {
    borderRadius: '10px',
  },
}

export const darkThemeOverrides: GlobalThemeOverrides = {
  common: {
    primaryColor: '#3AA6F2',
    primaryColorHover: '#56B4F4',
    primaryColorPressed: '#2A98E6',

    infoColor: '#56B4F4',
    successColor: '#43B980',
    warningColor: '#E0A63A',
    errorColor: '#E36A6A',

    bodyColor: 'transparent',
    cardColor: 'rgba(30, 36, 44, 0.7)',
    popoverColor: 'rgba(41, 50, 64, 0.85)',
    modalColor: 'rgba(48, 58, 73, 0.85)',

    textColorBase: '#E7EDF5',
    textColor1: '#E7EDF5',
    textColor2: '#C2CDDA',
    textColor3: '#97A6B8',
    textColorDisabled: '#6F7D8F',

    borderColor: 'rgba(85, 98, 116, 0.6)',
    dividerColor: 'rgba(74, 87, 105, 0.6)',

    borderRadius: '12px',
    borderRadiusSmall: '8px',
  },
  Button: {
    heightMedium: '40px',
    heightSmall: '32px',
    heightLarge: '48px',
    borderRadiusMedium: '10px',
    borderRadiusSmall: '8px',
    borderRadiusLarge: '12px',
    fontWeight: '600',
  },
  Input: {
    heightMedium: '40px',
    heightSmall: '32px',
    heightLarge: '48px',
    borderRadius: '10px',
    color: '#151c25',
    colorFocus: '#151c25',
    colorDisabled: '#232d39',
    textColor: '#E7EDF5',
    textColorDisabled: '#8fa0b4',
    border: '#6f8196',
    borderHover: '#8296ad',
    borderFocus: '#7ec3f5',
    borderDisabled: '#546375',
    boxShadowFocus: '0 0 0 2px rgba(126, 195, 245, 0.32)',
    placeholderColor: 'rgba(151, 166, 184, 0.82)',
    placeholderColorDisabled: 'rgba(143, 160, 180, 0.65)',
  },
  Select: {
    peers: {
      InternalSelection: {
        heightMedium: '40px',
        borderRadius: '10px',
        color: '#151c25',
        colorActive: '#151c25',
        colorDisabled: '#232d39',
        textColor: '#E7EDF5',
        textColorDisabled: '#8fa0b4',
        placeholderColor: 'rgba(151, 166, 184, 0.82)',
        border: '#6f8196',
        borderHover: '#8296ad',
        borderActive: '#7ec3f5',
        borderFocus: '#7ec3f5',
        boxShadowHover: '0 0 0 1px rgba(130, 150, 173, 0.3)',
        boxShadowActive: '0 0 0 2px rgba(126, 195, 245, 0.32)',
        boxShadowFocus: '0 0 0 2px rgba(126, 195, 245, 0.32)',
      },
    },
  },
  Card: {
    borderRadius: '14px',
    color: 'rgba(30, 36, 44, 0.7)',
  },
  Alert: {
    borderRadius: '10px',
  },
}
