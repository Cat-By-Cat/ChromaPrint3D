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

    bodyColor: '#F6F8FB',
    cardColor: '#FFFFFF',
    popoverColor: '#FFFFFF',
    modalColor: '#FFFFFF',

    textColorBase: '#1B2430',
    textColor1: '#1B2430',
    textColor2: '#4A5667',
    textColor3: '#6F7C8E',
    textColorDisabled: '#98A3B3',

    borderColor: '#D8E0EA',
    dividerColor: '#E9EEF5',

    borderRadius: '8px',
    borderRadiusSmall: '6px',
  },
  Button: {
    heightMedium: '40px',
    heightSmall: '32px',
    heightLarge: '48px',
    borderRadiusMedium: '8px',
    borderRadiusSmall: '6px',
    borderRadiusLarge: '12px',
    fontWeight: '600',
  },
  Input: {
    heightMedium: '40px',
    heightSmall: '32px',
    heightLarge: '48px',
    borderRadius: '8px',
    color: '#FFFFFF',
    placeholderColor: 'rgba(111, 124, 142, 0.72)',
  },
  Select: {
    peers: {
      InternalSelection: {
        heightMedium: '40px',
        borderRadius: '8px',
      },
    },
  },
  Card: {
    borderRadius: '8px',
    color: '#FFFFFF',
  },
  Alert: {
    borderRadius: '8px',
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

    bodyColor: '#171B21',
    cardColor: '#1E242C',
    popoverColor: '#293240',
    modalColor: '#303A49',

    textColorBase: '#E7EDF5',
    textColor1: '#E7EDF5',
    textColor2: '#C2CDDA',
    textColor3: '#97A6B8',
    textColorDisabled: '#6F7D8F',

    borderColor: '#556274',
    dividerColor: '#4A5769',

    borderRadius: '8px',
    borderRadiusSmall: '6px',
  },
  Button: {
    heightMedium: '40px',
    heightSmall: '32px',
    heightLarge: '48px',
    borderRadiusMedium: '8px',
    borderRadiusSmall: '6px',
    borderRadiusLarge: '12px',
    fontWeight: '600',
  },
  Input: {
    heightMedium: '40px',
    heightSmall: '32px',
    heightLarge: '48px',
    borderRadius: '8px',
    color: '#1E242C',
    border: '#647287',
    borderHover: '#77859A',
    borderFocus: '#6BC0F6',
    boxShadowFocus: '0 0 0 2px rgba(107, 192, 246, 0.32)',
    placeholderColor: 'rgba(151, 166, 184, 0.72)',
  },
  Select: {
    peers: {
      InternalSelection: {
        heightMedium: '40px',
        borderRadius: '8px',
        color: '#1E242C',
        border: '#647287',
        borderHover: '#77859A',
        borderFocus: '#6BC0F6',
        boxShadowFocus: '0 0 0 2px rgba(107, 192, 246, 0.32)',
      },
    },
  },
  Card: {
    borderRadius: '8px',
    color: '#1E242C',
  },
  Alert: {
    borderRadius: '8px',
  },
}
