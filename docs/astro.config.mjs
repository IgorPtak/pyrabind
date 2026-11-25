import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

export default defineConfig({
  site: 'https://igorptak.github.io',
  base: '/pyrabind',
  integrations: [
    starlight({
      title: 'pyrabind',
      description:
        'Write one C++20 nanobind-style binding module and compile natively for Python 3 and R 4.',
      logo: {
        src: './src/assets/logo.svg',
        alt: 'pyrabind logo -- Poznan potato (pyra) embracing Python and R',
      },
      favicon: '/favicon.svg',
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/IgorPtak/pyrabind',
        },
      ],
      editLink: {
        baseUrl: 'https://github.com/IgorPtak/pyrabind/edit/main/docs/',
      },
      customCss: ['./src/styles/custom.css'],
      sidebar: [
        { label: 'Home', link: '/' },
        { label: 'Getting Started & Build Guide', link: '/getting-started/' },
        { label: 'API Reference', link: '/api-reference/' },
        { label: 'Type Conversions', link: '/type-conversions/' },
        { label: 'NumPy, Eigen, Arrow & DataFrames', link: '/ndarray-and-dataframes/' },
        { label: 'Architecture', link: '/architecture/' },
        { label: 'Benchmarks', link: '/benchmarks/' },
      ],
    }),
  ],
});
